//  Copyright (c) 2012-2017 Fredrik Mellbin
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

#include "ffms.h"
#include "vapoursource4.h"
#include "VSHelper4.h"
#include "../core/utils.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <set>

static void VS_CC CreateIndex(const VSMap *in, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    FFMS_Init(0, 0);

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);
    int err;

    std::set<int> IndexTracks;

    const char *Source = vsapi->mapGetData(in, "source", 0, nullptr);
    const char *CacheFile = vsapi->mapGetData(in, "cachefile", 0, &err);

    int NumIndexTracks = vsapi->mapNumElements(in, "indextracks");
    bool IndexAllTracks = (NumIndexTracks == 1) && (vsapi->mapGetIntSaturated(in, "indextracks", 0, nullptr) == -1);
    if (!IndexAllTracks) {
        for (int i = 0; i < NumIndexTracks; i++) {
            int Track = vsapi->mapGetIntSaturated(in, "indextracks", i, nullptr);
            IndexTracks.insert(Track);
        }
    }

    int ErrorHandling = vsapi->mapGetIntSaturated(in, "errorhandling", 0, &err);
    if (err)
        ErrorHandling = FFMS_IEH_IGNORE;
    bool OverWrite = !!vsapi->mapGetInt(in, "overwrite", 0, &err);

    std::string DefaultCache(Source);
    if (!CacheFile || !strcmp(CacheFile, "")) {
        DefaultCache.append(".ffindex");
        CacheFile = DefaultCache.c_str();
    }

    bool EnableDrefs = !!vsapi->mapGetInt(in, "enable_drefs", 0, &err);
    bool UseAbsolutePath = !!vsapi->mapGetInt(in, "use_absolute_path", 0, &err);

    FFMS_Index *Index = FFMS_ReadIndex(CacheFile, &E);
    if (OverWrite || !Index || (Index && FFMS_IndexBelongsToFile(Index, Source, nullptr) != FFMS_ERROR_SUCCESS)) {
        FFMS_KeyValuePair LAVFOpts[] = {{ "enable_drefs", EnableDrefs ? "1" : "0" }, { "use_absolute_path", UseAbsolutePath ? "1" : "0" }};
        FFMS_Indexer *Indexer = FFMS_CreateIndexer2(Source, LAVFOpts, 2, &E);
        if (!Indexer) {
            FFMS_DestroyIndex(Index);
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());
        }

        if (IndexAllTracks) {
            FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);
        } else {
            for (int i : IndexTracks)
                FFMS_TrackIndexSettings(Indexer, i, 1, 0);
        }

        Index = FFMS_DoIndexing2(Indexer, ErrorHandling, &E);
        if (!Index)
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());
        if (FFMS_WriteIndex(CacheFile, Index, &E)) {
            FFMS_DestroyIndex(Index);
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());
        }
        FFMS_DestroyIndex(Index);
        if (!OverWrite)
            vsapi->mapSetData(out, "result", "Index generated", -1, dtUtf8, maReplace);
        else
            vsapi->mapSetData(out, "result", "Index generated (forced overwrite)", -1, dtUtf8, maReplace);
    } else {
        FFMS_DestroyIndex(Index);
        vsapi->mapSetData(out, "result", "Valid index already exists", -1, dtUtf8, maReplace);
    }
    FFMS_Deinit();
}

static void VS_CC CreateSource(const VSMap *in, VSMap *out, void *, VSCore *core, const VSAPI *vsapi) {
    FFMS_Init(0, 0);

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);
    int err;

    const char *Source = vsapi->mapGetData(in, "source", 0, nullptr);
    int Track = vsapi->mapGetIntSaturated(in, "track", 0, &err);
    if (err)
        Track = -1;
    bool Cache = !!vsapi->mapGetInt(in, "cache", 0, &err);
    if (err)
        Cache = true;
    const char *CacheFile = vsapi->mapGetData(in, "cachefile", 0, &err);
    int FPSNum = vsapi->mapGetIntSaturated(in, "fpsnum", 0, &err);
    if (err)
        FPSNum = -1;
    int FPSDen = vsapi->mapGetIntSaturated(in, "fpsden", 0, &err);
    if (err)
        FPSDen = 1;
    int Threads = vsapi->mapGetIntSaturated(in, "threads", 0, &err);
    const char *Timecodes = vsapi->mapGetData(in, "timecodes", 0, &err);
    int SeekMode = vsapi->mapGetIntSaturated(in, "seekmode", 0, &err);
    if (err)
        SeekMode = FFMS_SEEK_NORMAL;
    int Width = vsapi->mapGetIntSaturated(in, "width", 0, &err);
    int Height = vsapi->mapGetIntSaturated(in, "height", 0, &err);
    const char *Resizer = vsapi->mapGetData(in, "resizer", 0, &err);
    if (err)
        Resizer = "BICUBIC";
    int Format = vsapi->mapGetIntSaturated(in, "format", 0, &err);

    bool OutputAlpha = !!vsapi->mapGetInt(in, "alpha", 0, &err);

    if (FPSDen < 1)
        return vsapi->mapSetError(out, "Source: FPS denominator needs to be 1 or higher");
    if (Track <= -2)
        return vsapi->mapSetError(out, "Source: No video track selected");
    if (SeekMode < -1 || SeekMode > 3)
        return vsapi->mapSetError(out, "Source: Invalid seekmode selected");
    if (Timecodes && IsSamePath(Source, Timecodes))
        return vsapi->mapSetError(out, "Source: Timecodes will overwrite the source");

    FFMS_Index *Index = nullptr;
    std::string DefaultCache;
    if (Cache) {
        if (CacheFile && *CacheFile) {
            if (IsSamePath(Source, CacheFile))
                return vsapi->mapSetError(out, "Source: Cache will overwrite the source");
            Index = FFMS_ReadIndex(CacheFile, &E);
        } else {
            DefaultCache = Source;
            DefaultCache += ".ffindex";
            CacheFile = DefaultCache.c_str();
            Index = FFMS_ReadIndex(CacheFile, &E);
            // Reindex if the index doesn't match the file and its name wasn't
            // explicitly given
            if (Index && FFMS_IndexBelongsToFile(Index, Source, nullptr) != FFMS_ERROR_SUCCESS) {
                FFMS_DestroyIndex(Index);
                Index = nullptr;
            }
        }
    }

    if (!Index) {
        FFMS_Indexer *Indexer = FFMS_CreateIndexer(Source, &E);
        if (!Indexer)
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());

        Index = FFMS_DoIndexing2(Indexer, FFMS_IEH_CLEAR_TRACK, &E);
        if (!Index)
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());

        if (Cache)
            if (FFMS_WriteIndex(CacheFile, Index, &E)) {
                FFMS_DestroyIndex(Index);
                return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());
            }
    }

    if (Track == -1)
        Track = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_VIDEO, &E);
    if (Track < 0) {
        FFMS_DestroyIndex(Index);
        return vsapi->mapSetError(out, "Source: No video track found");
    }

    if (Timecodes && strcmp(Timecodes, "")) {
        if (FFMS_WriteTimecodes(FFMS_GetTrackFromIndex(Index, Track), Timecodes, &E)) {
            FFMS_DestroyIndex(Index);
            return vsapi->mapSetError(out, (std::string("Index: ") + E.Buffer).c_str());
        }
    }

    VSVideoSource4 *vs;
    try {
        vs = new VSVideoSource4(Source, Track, Index, FPSNum, FPSDen, Threads, SeekMode, Width, Height, Resizer, Format, OutputAlpha, vsapi, core);
    } catch (std::exception const& e) {
        FFMS_DestroyIndex(Index);
        return vsapi->mapSetError(out, e.what());
    }

    VSNode *node = vsapi->createVideoFilter2("Source", vs->GetVideoInfo(), VSVideoSource4::GetFrame, VSVideoSource4::Free, fmUnordered, nullptr, 0, vs, core);
    vs->SetCacheThreshold(vsapi->setLinearFilter(node));
    vsapi->mapConsumeNode(out, "clip", node, maAppend);

    FFMS_DestroyIndex(Index);
}

static void VS_CC GetLogLevel(const VSMap *, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    vsapi->mapSetInt(out, "level", FFMS_GetLogLevel(), maReplace);
}

static void VS_CC SetLogLevel(const VSMap *in, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    FFMS_SetLogLevel(vsapi->mapGetIntSaturated(in, "level", 0, nullptr));
    vsapi->mapSetInt(out, "level", FFMS_GetLogLevel(), maReplace);
}

static void VS_CC GetVersion(const VSMap *, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    int Version = FFMS_GetVersion();
    char buf[100];
    sprintf(buf, "%d.%d.%d.%d", Version >> 24, (Version & 0xFF0000) >> 16, (Version & 0xFF00) >> 8, Version & 0xFF);
    vsapi->mapSetData(out, "version", buf, -1, maReplace, dtUtf8);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.ffms2", "ffms2", "FFmpegSource 2 for VapourSynth", FFMS_GetVersion(), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Index", "source:data;cachefile:data:opt;indextracks:int[]:opt;errorhandling:int:opt;overwrite:int:opt;enable_drefs:int:opt;use_absolute_path:int:opt;", "result:data;", CreateIndex, nullptr, plugin);
    vspapi->registerFunction("Source", "source:data;track:int:opt;cache:int:opt;cachefile:data:opt;fpsnum:int:opt;fpsden:int:opt;threads:int:opt;timecodes:data:opt;seekmode:int:opt;width:int:opt;height:int:opt;resizer:data:opt;format:int:opt;alpha:int:opt;", "clip:vnode;", CreateSource, nullptr, plugin);
    vspapi->registerFunction("GetLogLevel", "", "level:int;", GetLogLevel, nullptr, plugin);
    vspapi->registerFunction("SetLogLevel", "level:int;", "level:int;", SetLogLevel, nullptr, plugin);
    vspapi->registerFunction("Version", "", "version:data;", GetVersion, nullptr, plugin);
}
