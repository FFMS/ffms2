//  Copyright (c) 2007-2017 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include <string>
#include "ffms.h"
#include "avssources.h"
#include "../core/utils.h"

#ifdef _WIN32
#define AVS_EXPORT __declspec(dllexport)
#else
#define AVS_EXPORT __attribute__((visibility("default")))
#endif

static AVSValue __cdecl CreateFFIndex(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    if (!Args[0].Defined())
        Env->ThrowError("FFIndex: No source specified");

    FFMS_Init(0, 0);

    const char *Source = Args[0].AsString();
    const char *CacheFile = Args[1].AsString("");
    int IndexMask = Args[2].AsInt(-1);
    int ErrorHandling = Args[3].AsInt(FFMS_IEH_IGNORE);
    bool OverWrite = Args[4].AsBool(false);
    bool EnableDrefs = Args[5].AsBool(false);
    bool UseAbsolutePath = Args[6].AsBool(false);

    std::string DefaultCache(Source);
    DefaultCache.append(".ffindex");
    if (!strcmp(CacheFile, ""))
        CacheFile = DefaultCache.c_str();

    ErrorInfo E;
    FFMS_Index *Index = FFMS_ReadIndex(CacheFile, &E);
    if (OverWrite || !Index || (Index && FFMS_IndexBelongsToFile(Index, Source, 0) != FFMS_ERROR_SUCCESS)) {
        FFMS_KeyValuePair LAVFOpts[] = {{ "enable_drefs", EnableDrefs ? "1" : "0" }, { "use_absolute_path", UseAbsolutePath ? "1" : "0" }};
        FFMS_Indexer *Indexer = FFMS_CreateIndexer2(Source, LAVFOpts, 2, &E);
        if (!Indexer)
            Env->ThrowError("FFIndex: %s", E.Buffer);

        // Treat -1 as meaning track numbers above sizeof(int) too
        if (IndexMask == -1)
            FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);

        // Apply attributes to remaining tracks (will set the attributes again on some tracks)
        for (int i = 0; i < sizeof(IndexMask) * 8; i++) {
            if ((IndexMask >> i) & 1)
                FFMS_TrackIndexSettings(Indexer, i, 1, 0);
        }

        Index = FFMS_DoIndexing2(Indexer, ErrorHandling, &E);
        if (!Index)
            Env->ThrowError("FFIndex: %s", E.Buffer);
        if (FFMS_WriteIndex(CacheFile, Index, &E)) {
            FFMS_DestroyIndex(Index);
            Env->ThrowError("FFIndex: %s", E.Buffer);
        }
        FFMS_DestroyIndex(Index);
        if (!OverWrite)
            return AVSValue(1);
        else
            return AVSValue(2);
    } else {
        FFMS_DestroyIndex(Index);
        return AVSValue(0);
    }
}

static AVSValue __cdecl CreateFFVideoSource(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    FFMS_Init(0, 0);

    if (!Args[0].Defined())
        Env->ThrowError("FFVideoSource: No source specified");

    const char *Source = Args[0].AsString();
    int Track = Args[1].AsInt(-1);
    bool Cache = Args[2].AsBool(true);
    const char *CacheFile = Args[3].AsString("");
    int FPSNum = Args[4].AsInt(-1);
    int FPSDen = Args[5].AsInt(1);
    int Threads = Args[6].AsInt(-1);
    const char *Timecodes = Args[7].AsString("");
    int SeekMode = Args[8].AsInt(1);
    int RFFMode = Args[9].AsInt(0);
    int Width = Args[10].AsInt(0);
    int Height = Args[11].AsInt(0);
    const char *Resizer = Args[12].AsString("BICUBIC");
    const char *ColorSpace = Args[13].AsString("");
    const char *VarPrefix = Args[14].AsString("");

    if (FPSDen < 1)
        Env->ThrowError("FFVideoSource: FPS denominator needs to be 1 or higher");

    if (Track <= -2)
        Env->ThrowError("FFVideoSource: No video track selected");

    if (SeekMode < -1 || SeekMode > 3)
        Env->ThrowError("FFVideoSource: Invalid seekmode selected");

    if (RFFMode != 0)
        Env->ThrowError("FFVideoSource: Invalid RFF mode selected");

    if (IsSamePath(Source, Timecodes))
        Env->ThrowError("FFVideoSource: Timecodes will overwrite the source");

    ErrorInfo E;
    FFMS_Index *Index = nullptr;
    std::string DefaultCache;
    if (Cache) {
        if (*CacheFile) {
            if (IsSamePath(Source, CacheFile))
                Env->ThrowError("FFVideoSource: Cache will overwrite the source");
            Index = FFMS_ReadIndex(CacheFile, &E);
        } else {
            DefaultCache = Source;
            DefaultCache += ".ffindex";
            CacheFile = DefaultCache.c_str();
            Index = FFMS_ReadIndex(CacheFile, &E);
            // Reindex if the index doesn't match the file and its name wasn't
            // explicitly given
            if (Index && FFMS_IndexBelongsToFile(Index, Source, 0) != FFMS_ERROR_SUCCESS) {
                FFMS_DestroyIndex(Index);
                Index = 0;
            }
        }
    }

    if (!Index) {
        FFMS_Indexer *Indexer = FFMS_CreateIndexer(Source, &E);
        if (!Indexer)
            Env->ThrowError("FFVideoSource: %s", E.Buffer);

        Index = FFMS_DoIndexing2(Indexer, FFMS_IEH_CLEAR_TRACK, &E);
        if (!Index)
            Env->ThrowError("FFVideoSource: %s", E.Buffer);

        if (Cache)
            if (FFMS_WriteIndex(CacheFile, Index, &E)) {
                FFMS_DestroyIndex(Index);
                Env->ThrowError("FFVideoSource: %s", E.Buffer);
            }
    }

    if (Track == -1)
        Track = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_VIDEO, &E);
    if (Track < 0)
        Env->ThrowError("FFVideoSource: No video track found");

    if (strcmp(Timecodes, "")) {
        if (FFMS_WriteTimecodes(FFMS_GetTrackFromIndex(Index, Track), Timecodes, &E)) {
            FFMS_DestroyIndex(Index);
            Env->ThrowError("FFVideoSource: %s", E.Buffer);
        }
    }

    AvisynthVideoSource *Filter;

    try {
        Filter = new AvisynthVideoSource(Source, Track, Index, FPSNum, FPSDen, Threads, SeekMode, Width, Height, Resizer, ColorSpace, VarPrefix, Env);
    } catch (...) {
        FFMS_DestroyIndex(Index);
        throw;
    }

    FFMS_DestroyIndex(Index);
    return Filter;
}

static AVSValue __cdecl CreateFFAudioSource(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    FFMS_Init(0, 0);

    if (!Args[0].Defined())
        Env->ThrowError("FFAudioSource: No source specified");

    const char *Source = Args[0].AsString();
    int Track = Args[1].AsInt(-1);
    bool Cache = Args[2].AsBool(true);
    const char *CacheFile = Args[3].AsString("");
    int AdjustDelay = Args[4].AsInt(-1);
    int FillGaps = Args[5].AsInt(-1);
    double DrcScale = Args[6].AsFloat(0);
    const char *VarPrefix = Args[7].AsString("");

    if (Track <= -2)
        Env->ThrowError("FFAudioSource: No audio track selected");

    ErrorInfo E;
    FFMS_Index *Index = nullptr;
    std::string DefaultCache;
    if (Cache) {
        if (*CacheFile) {
            if (IsSamePath(Source, CacheFile))
                Env->ThrowError("FFAudioSource: Cache will overwrite the source");
            Index = FFMS_ReadIndex(CacheFile, &E);
        } else {
            DefaultCache = Source;
            DefaultCache += ".ffindex";
            CacheFile = DefaultCache.c_str();
            Index = FFMS_ReadIndex(CacheFile, &E);
            // Reindex if the index doesn't match the file and its name wasn't
            // explicitly given
            if (Index && FFMS_IndexBelongsToFile(Index, Source, 0) != FFMS_ERROR_SUCCESS) {
                FFMS_DestroyIndex(Index);
                Index = 0;
            }
        }
    }

    // Index needs to be remade if it is an unindexed audio track
    if (Index && Track >= 0 && Track < FFMS_GetNumTracks(Index)
        && FFMS_GetTrackType(FFMS_GetTrackFromIndex(Index, Track)) == FFMS_TYPE_AUDIO
        && FFMS_GetNumFrames(FFMS_GetTrackFromIndex(Index, Track)) == 0) {
        FFMS_DestroyIndex(Index);
        Index = nullptr;
    }

    // More complicated for finding a default track, reindex the file if at least one audio track exists
    if (Index && FFMS_GetFirstTrackOfType(Index, FFMS_TYPE_AUDIO, &E) >= 0
        && FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_AUDIO, &E) < 0) {
        for (int i = 0; i < FFMS_GetNumTracks(Index); i++) {
            if (FFMS_GetTrackType(FFMS_GetTrackFromIndex(Index, i)) == FFMS_TYPE_AUDIO) {
                FFMS_DestroyIndex(Index);
                Index = nullptr;
                break;
            }
        }
    }

    if (!Index) {
        FFMS_Indexer *Indexer = FFMS_CreateIndexer(Source, &E);
        if (!Indexer)
            Env->ThrowError("FFAudioSource: %s", E.Buffer);

        FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);

        Index = FFMS_DoIndexing2(Indexer, FFMS_IEH_CLEAR_TRACK, &E);
        if (!Index)
            Env->ThrowError("FFAudioSource: %s", E.Buffer);

        if (Cache)
            if (FFMS_WriteIndex(CacheFile, Index, &E)) {
                FFMS_DestroyIndex(Index);
                Env->ThrowError("FFAudioSource: %s", E.Buffer);
            }
    }

    if (Track == -1)
        Track = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_AUDIO, &E);
    if (Track < 0)
        Env->ThrowError("FFAudioSource: No audio track found");

    if (AdjustDelay < -3)
        Env->ThrowError("FFAudioSource: Invalid delay adjustment specified");
    if (AdjustDelay >= FFMS_GetNumTracks(Index))
        Env->ThrowError("FFAudioSource: Invalid track to calculate delay from specified");

    AvisynthAudioSource *Filter;

    try {
        Filter = new AvisynthAudioSource(Source, Track, Index, AdjustDelay, FillGaps, DrcScale, VarPrefix, Env);
    } catch (...) {
        FFMS_DestroyIndex(Index);
        throw;
    }

    FFMS_DestroyIndex(Index);
    return Filter;
}

static AVSValue __cdecl CreateFFmpegSource2(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    const char *FFIArgNames[] = { "source", "cachefile", "indexmask", "overwrite", "enable_drefs", "use_absolute_path"};
    const char *FFVArgNames[] = { "source", "track", "cache", "cachefile", "fpsnum", "fpsden", "threads", "timecodes", "seekmode", "rffmode", "width", "height", "resizer", "colorspace", "varprefix" };
    const char *FFAArgNames[] = { "source", "track", "cache", "cachefile", "adjustdelay", "fill_gaps", "drc_scale", "varprefix"};

    bool Cache = Args[3].AsBool(true);
    bool WithAudio = Args[2].AsInt(-2) > -2;
    if (Cache) {
        AVSValue FFIArgs[] = { Args[0], Args[4], WithAudio ? -1 : 0, Args[10], Args[17], Args[18]};
        static_assert((sizeof(FFIArgs) / sizeof(FFIArgs[0])) == (sizeof(FFIArgNames) / sizeof(FFIArgNames[0])), "Arg error");
        Env->Invoke("FFIndex", AVSValue(FFIArgs, sizeof(FFIArgs) / sizeof(FFIArgs[0])), FFIArgNames);
    }

    AVSValue FFVArgs[] = { Args[0], Args[1], Args[3], Args[4], Args[5], Args[6], Args[7], Args[8], Args[9], Args[15], Args[11], Args[12], Args[13], Args[14], Args[21] };
    static_assert((sizeof(FFVArgs) / sizeof(FFVArgs[0])) == (sizeof(FFVArgNames) / sizeof(FFVArgNames[0])), "Arg error");
    AVSValue Video = Env->Invoke("FFVideoSource", AVSValue(FFVArgs, sizeof(FFVArgs) / sizeof(FFVArgs[0])), FFVArgNames);

    AVSValue Audio;
    if (WithAudio) {
        AVSValue FFAArgs[] = { Args[0], Args[2], Args[3], Args[4], Args[16], Args[19], Args[20], Args[21] };
        static_assert((sizeof(FFAArgs) / sizeof(FFAArgs[0])) == (sizeof(FFAArgNames) / sizeof(FFAArgNames[0])), "Arg error");
        Audio = Env->Invoke("FFAudioSource", AVSValue(FFAArgs, sizeof(FFAArgs) / sizeof(FFAArgs[0])), FFAArgNames);
        AVSValue ADArgs[] = { Video, Audio };
        return Env->Invoke("AudioDubEx", AVSValue(ADArgs, sizeof(ADArgs) / sizeof(ADArgs[0])));
    } else {
        return Video;
    }
}

static AVSValue __cdecl CreateFFImageSource(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    const char *FFISArgNames[] = { "source", "width", "height", "resizer", "colorspace", "varprefix", "cache", "seekmode" };
    AVSValue FFISArgs[] = { Args[0], Args[1], Args[2], Args[3], Args[4], Args[5], false, -1 };
    static_assert((sizeof(FFISArgs) / sizeof(FFISArgs[0])) == (sizeof(FFISArgNames) / sizeof(FFISArgNames[0])), "Arg error");
    return Env->Invoke("FFVideoSource", AVSValue(FFISArgs, sizeof(FFISArgs) / sizeof(FFISArgs[0])), FFISArgNames);
}

static AVSValue __cdecl CreateFFCopyrightInfringement(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    const char *ArgNames[] = { "source" };
    Env->Invoke("FFIndex", Args, ArgNames);
    AVSValue ExArgs[2] = { Env->Invoke("FFVideoSource", Args, ArgNames), Env->Invoke("FFAudioSource", Args, ArgNames) };
    return Env->Invoke("AudioDubEx", AVSValue(ExArgs, sizeof(ExArgs) / sizeof(ExArgs[0])));
}

static AVSValue __cdecl FFGetLogLevel(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    return FFMS_GetLogLevel();
}

static AVSValue __cdecl FFSetLogLevel(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    FFMS_SetLogLevel(Args[0].AsInt());
    return FFMS_GetLogLevel();
}

static AVSValue __cdecl FFGetVersion(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    int Version = FFMS_GetVersion();
    return Env->Sprintf("%d.%d.%d.%d", Version >> 24, (Version & 0xFF0000) >> 16, (Version & 0xFF00) >> 8, Version & 0xFF);
}

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" AVS_EXPORT const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* Env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    Env->AddFunction("FFIndex", "[source]s[cachefile]s[indexmask]i[errorhandling]i[overwrite]b[enable_drefs]b[use_absolute_path]b", CreateFFIndex, nullptr);
    Env->AddFunction("FFVideoSource", "[source]s[track]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[threads]i[timecodes]s[seekmode]i[rffmode]i[width]i[height]i[resizer]s[colorspace]s[varprefix]s", CreateFFVideoSource, nullptr);
    Env->AddFunction("FFAudioSource", "[source]s[track]i[cache]b[cachefile]s[adjustdelay]i[fill_gaps]i[drc_scale]f[varprefix]s", CreateFFAudioSource, nullptr);

    Env->AddFunction("FFmpegSource2", "[source]s[vtrack]i[atrack]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[threads]i[timecodes]s[seekmode]i[overwrite]b[width]i[height]i[resizer]s[colorspace]s[rffmode]i[adjustdelay]i[enable_drefs]b[use_absolute_path]b[fill_gaps]i[drc_scale]f[varprefix]s", CreateFFmpegSource2, nullptr);
    Env->AddFunction("FFMS2", "[source]s[vtrack]i[atrack]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[threads]i[timecodes]s[seekmode]i[overwrite]b[width]i[height]i[resizer]s[colorspace]s[rffmode]i[adjustdelay]i[enable_drefs]b[use_absolute_path]b[fill_gaps]i[drc_scale]f[varprefix]s", CreateFFmpegSource2, nullptr);

    Env->AddFunction("FFImageSource", "[source]s[width]i[height]i[resizer]s[colorspace]s[varprefix]s", CreateFFImageSource, nullptr);
    Env->AddFunction("FFCopyrightInfringement", "[source]s", CreateFFCopyrightInfringement, nullptr);

    Env->AddFunction("FFGetLogLevel", "", FFGetLogLevel, nullptr);
    Env->AddFunction("FFSetLogLevel", "i", FFSetLogLevel, nullptr);
    Env->AddFunction("FFGetVersion", "", FFGetVersion, nullptr);

    return "FFmpegSource - The Second Coming V2.0 Final";
}
