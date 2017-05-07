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
#include "vapoursource.h"
#include "VSHelper.h"
#include "../core/utils.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <set>

#include <mpls_parse.h>
#include <libbluray/bluray.h>

static void VS_CC CreateIndex(const VSMap *in, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    FFMS_Init(0, 0);

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);
    int err;

    std::set<int> IndexTracks;

    const char *Source = vsapi->propGetData(in, "source", 0, nullptr);
    const char *CacheFile = vsapi->propGetData(in, "cachefile", 0, &err);

    int NumIndexTracks = vsapi->propNumElements(in, "indextracks");
    bool IndexAllTracks = (NumIndexTracks == 1) && (int64ToIntS(vsapi->propGetInt(in, "indextracks", 0, nullptr)) == -1);
    if (!IndexAllTracks) {
        for (int i = 0; i < NumIndexTracks; i++) {
            int Track = int64ToIntS(vsapi->propGetInt(in, "indextracks", i, nullptr));
            IndexTracks.insert(Track);
        }
    }

    int ErrorHandling = int64ToIntS(vsapi->propGetInt(in, "errorhandling", 0, &err));
    if (err)
        ErrorHandling = FFMS_IEH_IGNORE;
    bool OverWrite = !!vsapi->propGetInt(in, "overwrite", 0, &err);

    std::string DefaultCache(Source);
    if (!CacheFile || !strcmp(CacheFile, "")) {
        DefaultCache.append(".ffindex");
        CacheFile = DefaultCache.c_str();
    }

    FFMS_Index *Index = FFMS_ReadIndex(CacheFile, &E);
    if (OverWrite || !Index || (Index && FFMS_IndexBelongsToFile(Index, Source, nullptr) != FFMS_ERROR_SUCCESS)) {
        FFMS_Indexer *Indexer = FFMS_CreateIndexer(Source, &E);
        if (!Indexer) {
            FFMS_DestroyIndex(Index);
            return vsapi->setError(out, (std::string("Index: ") + E.Buffer).c_str());
        }

        if (IndexAllTracks) {
            FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);
        } else {
            for (int i : IndexTracks)
                FFMS_TrackIndexSettings(Indexer, i, 1, 0);
        }

        Index = FFMS_DoIndexing2(Indexer, ErrorHandling, &E);
        if (!Index)
            return vsapi->setError(out, (std::string("Index: ") + E.Buffer).c_str());
        if (FFMS_WriteIndex(CacheFile, Index, &E)) {
            FFMS_DestroyIndex(Index);
            return vsapi->setError(out, (std::string("Index: ") + E.Buffer).c_str());
        }
        FFMS_DestroyIndex(Index);
        if (!OverWrite)
            vsapi->propSetData(out, "result", "Index generated", -1, paReplace);
        else
            vsapi->propSetData(out, "result", "Index generated (forced overwrite)", -1, paReplace);
    } else {
        FFMS_DestroyIndex(Index);
        vsapi->propSetData(out, "result", "Valid index already exists", -1, paReplace);
    }
}

static void VS_CC CreateSource(const VSMap *in, VSMap **out, const char *userData, VSCore *core, const VSAPI *vsapi) {
    FFMS_Init(0, 0);

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);
    int err;

    const char *Source = userData;
    int Track = int64ToIntS(vsapi->propGetInt(in, "track", 0, &err));
    if (err)
        Track = -1;
    bool Cache = !!vsapi->propGetInt(in, "cache", 0, &err);
    if (err)
        Cache = true;
    const char *CacheFile = vsapi->propGetData(in, "cachefile", 0, &err);
    int FPSNum = int64ToIntS(vsapi->propGetInt(in, "fpsnum", 0, &err));
    if (err)
        FPSNum = -1;
    int FPSDen = int64ToIntS(vsapi->propGetInt(in, "fpsden", 0, &err));
    if (err)
        FPSDen = 1;
    int Threads = int64ToIntS(vsapi->propGetInt(in, "threads", 0, &err));
    const char *Timecodes = vsapi->propGetData(in, "timecodes", 0, &err);
    int SeekMode = int64ToIntS(vsapi->propGetInt(in, "seekmode", 0, &err));
    if (err)
        SeekMode = FFMS_SEEK_NORMAL;
    int RFFMode = int64ToIntS(vsapi->propGetInt(in, "rffmode", 0, &err));
    int Width = int64ToIntS(vsapi->propGetInt(in, "width", 0, &err));
    int Height = int64ToIntS(vsapi->propGetInt(in, "height", 0, &err));
    const char *Resizer = vsapi->propGetData(in, "resizer", 0, &err);
    if (err)
        Resizer = "BICUBIC";
    int Format = int64ToIntS(vsapi->propGetInt(in, "format", 0, &err));
    bool OutputAlpha = !!vsapi->propGetInt(in, "alpha", 0, &err);
    if (err)
        OutputAlpha = false;

    if (FPSDen < 1)
        return vsapi->setError(*out, "Source: FPS denominator needs to be 1 or higher");
    if (Track <= -2)
        return vsapi->setError(*out, "Source: No video track selected");
    if (SeekMode < -1 || SeekMode > 3)
        return vsapi->setError(*out, "Source: Invalid seekmode selected");
    if (RFFMode < 0 || RFFMode > 2)
        return vsapi->setError(*out, "Source: Invalid RFF mode selected");
    if (RFFMode > 0 && FPSNum > 0)
        return vsapi->setError(*out, "Source: RFF modes may not be combined with CFR conversion");
    if (Timecodes && IsSamePath(Source, Timecodes))
        return vsapi->setError(*out, "Source: Timecodes will overwrite the source");

    FFMS_Index *Index = nullptr;
    std::string DefaultCache;
    if (Cache) {
        if (CacheFile && *CacheFile) {
            if (IsSamePath(Source, CacheFile))
                return vsapi->setError(*out, "Source: Cache will overwrite the source");
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
            return vsapi->setError(*out, (std::string("Index: ") + E.Buffer).c_str());

        Index = FFMS_DoIndexing2(Indexer, FFMS_IEH_CLEAR_TRACK, &E);
        if (!Index)
            return vsapi->setError(*out, (std::string("Index: ") + E.Buffer).c_str());

        if (Cache)
            if (FFMS_WriteIndex(CacheFile, Index, &E)) {
                FFMS_DestroyIndex(Index);
                return vsapi->setError(*out, (std::string("Index: ") + E.Buffer).c_str());
            }
    }

    if (Track == -1)
        Track = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_VIDEO, &E);
    if (Track < 0) {
        FFMS_DestroyIndex(Index);
        return vsapi->setError(*out, "Source: No video track found");
    }

    if (Timecodes && strcmp(Timecodes, "")) {
        if (FFMS_WriteTimecodes(FFMS_GetTrackFromIndex(Index, Track), Timecodes, &E)) {
            FFMS_DestroyIndex(Index);
            return vsapi->setError(*out, (std::string("Index: ") + E.Buffer).c_str());
        }
    }

    VSVideoSource *vs;
    try {
        vs = new VSVideoSource(Source, Track, Index, FPSNum, FPSDen, Threads, SeekMode, RFFMode, Width, Height, Resizer, Format, OutputAlpha, vsapi, core);
    } catch (std::exception const& e) {
        FFMS_DestroyIndex(Index);
        return vsapi->setError(*out, e.what());
    }

    vsapi->createFilter(in, *out, "Source", VSVideoSource::Init, VSVideoSource::GetFrame, VSVideoSource::Free, fmUnordered, nfMakeLinear, vs, core);

    FFMS_DestroyIndex(Index);
}

static void VS_CC PreSource(const VSMap *in, VSMap *out, void *, VSCore *core, const VSAPI *vsapi) {
    int err;

    const char *CacheFile = vsapi->propGetData(in, "cachefile", 0, &err);
    const char *Timecodes = vsapi->propGetData(in, "timecodes", 0, &err);

    const std::string Source(vsapi->propGetData(in, "source", 0, nullptr));
    const bool IsMpls = (Source.size() > 5) && (Source.rfind(".mpls") == Source.size() - 5);

    MPLS_PL *pl = nullptr;
    VSNodeRef **Clip = nullptr;
    unsigned ClipCount = 1;
    if (IsMpls) {
        if (CacheFile)
            return vsapi->setError(out, "Source: Specify cachefile for mpls file is invalid");
        if (Timecodes)
            return vsapi->setError(out, "Source: Specify timecodes for mpls file is invalid");

        pl = bd_read_mpls(Source.c_str());
        if (!pl)
            return vsapi->setError(out, "Source: Failed to open mpls file");

        Clip = new VSNodeRef *[pl->list_count];
        ClipCount = pl->list_count;
    } else {
        Clip = new VSNodeRef *[1];
    }

    for (unsigned i = 0; i < ClipCount; i++) {
        std::string File = Source;
        if (IsMpls)
            File = Source.substr(0, Source.find_last_of("/\\") - 8) + "STREAM/" + pl->play_item[i].clip[0].clip_id + ".m2ts";

        CreateSource(in, &out, File.c_str(), core, vsapi);

        if (vsapi->getError(out))
            return;

        Clip[i] = vsapi->propGetNode(out, "clip", 0, nullptr);
        vsapi->clearMap(out);
    }

    if (ClipCount > 1) {
        VSMap *Args = vsapi->createMap();

        vsapi->propSetNode(Args, "clips", Clip[0], paReplace);
        vsapi->freeNode(Clip[0]);
        for (unsigned i = 1; i < ClipCount; i++) {
            vsapi->propSetNode(Args, "clips", Clip[i], paAppend);
            vsapi->freeNode(Clip[i]);
        }

        VSMap *Ret = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.std", core), "Splice", Args);
        VSNodeRef *Node = vsapi->propGetNode(Ret, "clip", 0, nullptr);
        vsapi->propSetNode(out, "clip", Node, paReplace);
        vsapi->freeNode(Node);
        vsapi->freeMap(Args);
        vsapi->freeMap(Ret);
    } else {
        vsapi->propSetNode(out, "clip", Clip[0], paReplace);
        vsapi->freeNode(Clip[0]);
    }

    bd_free_mpls(pl);
    delete[] Clip;
}

static void VS_CC GetLogLevel(const VSMap *, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    vsapi->propSetInt(out, "level", FFMS_GetLogLevel(), paReplace);
}

static void VS_CC SetLogLevel(const VSMap *in, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    FFMS_SetLogLevel((int)vsapi->propGetInt(in, "level", 0, nullptr));
    vsapi->propSetInt(out, "level", FFMS_GetLogLevel(), paReplace);
}

static void VS_CC GetVersion(const VSMap *, VSMap *out, void *, VSCore *, const VSAPI *vsapi) {
    int Version = FFMS_GetVersion();
    char buf[100];
    sprintf(buf, "%d.%d.%d.%d", Version >> 24, (Version & 0xFF0000) >> 16, (Version & 0xFF00) >> 8, Version & 0xFF);
    vsapi->propSetData(out, "version", buf, -1, paReplace);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vapoursynth.ffms2", "ffms2", "FFmpegSource 2 for VapourSynth", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Index", "source:data;cachefile:data:opt;indextracks:int[]:opt;errorhandling:int:opt;overwrite:int:opt;", CreateIndex, nullptr, plugin);
    registerFunc("Source", "source:data;track:int:opt;cache:int:opt;cachefile:data:opt;fpsnum:int:opt;fpsden:int:opt;threads:int:opt;timecodes:data:opt;seekmode:int:opt;width:int:opt;height:int:opt;resizer:data:opt;format:int:opt;alpha:int:opt;", PreSource, nullptr, plugin);
    registerFunc("GetLogLevel", "", GetLogLevel, nullptr, plugin);
    registerFunc("SetLogLevel", "level:int;", SetLogLevel, nullptr, plugin);
    registerFunc("Version", "", GetVersion, nullptr, plugin);
}
