//  Copyright (c) 2012 Fredrik Mellbin
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

#include <cstdio>
#include <cstring>
#include <string>
#include "ffms.h"
#include "vapoursource.h"

// assume windows is the only OS with a case insensitive filesystem
#ifndef _WIN32
#define _stricmp strcmp
#endif

static void VS_CC CreateIndex(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)  {
	FFMS_Init(0,  1);

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);
	int err;

	const char *Source = vsapi->propGetData(in, "source", 0, 0);
	const char *CacheFile = vsapi->propGetData(in, "cachefile", 0, &err);
	int IndexMask = (int)vsapi->propGetInt(in, "indexmask", 0, &err);
	if (err)
		IndexMask = -1;
	int DumpMask = (int)vsapi->propGetInt(in, "dumpmask", 0, &err);
	const char *AudioFile = vsapi->propGetData(in, "audiofile", 0, &err);
	if (err)
		AudioFile = "%sourcefile%.%trackzn%.w64";
	int ErrorHandling = (int)vsapi->propGetInt(in, "errorhandling", 0, &err);
	if (err)
		ErrorHandling = FFMS_IEH_IGNORE;
	bool OverWrite = !!vsapi->propGetInt(in, "overwrite", 0, &err);
	const char *DemuxerStr = vsapi->propGetData(in, "demuxer", 0, &err);

	std::string DefaultCache(Source);
	if (!CacheFile || !strcmp(CacheFile, "")) {
		DefaultCache.append(".ffindex");
		CacheFile = DefaultCache.c_str();
	}

	if (!AudioFile || !strcmp(AudioFile, "")) {
		vsapi->setError(out, "Index: Specifying an empty audio filename is not allowed");
		return;
	}

	int Demuxer;
	if (!DemuxerStr)
		Demuxer = FFMS_SOURCE_DEFAULT;
	else if (!strcmp(DemuxerStr, "lavf"))
		Demuxer = FFMS_SOURCE_LAVF;
	else if (!strcmp(DemuxerStr, "matroska"))
		Demuxer = FFMS_SOURCE_MATROSKA;
	else if (!strcmp(DemuxerStr, "haalimpeg"))
		Demuxer = FFMS_SOURCE_HAALIMPEG;
	else if (!strcmp(DemuxerStr, "haaliogg"))
		Demuxer = FFMS_SOURCE_HAALIOGG;
	else {
		vsapi->setError(out, "Index: Invalid demuxer requested");
		return;
	}

	FFMS_Index *Index = FFMS_ReadIndex(CacheFile, &E);
	if (OverWrite || !Index || (Index && FFMS_IndexBelongsToFile(Index, Source, 0) != FFMS_ERROR_SUCCESS)) {
		FFMS_Indexer *Indexer = FFMS_CreateIndexerWithDemuxer(Source, Demuxer, &E);
		if (!Indexer) {
			std::string buf = "Index: ";
			buf += E.Buffer;
			vsapi->setError(out, buf.c_str());
			return;
		}
		if (!(Index = FFMS_DoIndexing(Indexer, IndexMask, DumpMask, FFMS_DefaultAudioFilename, (void *)AudioFile, ErrorHandling, NULL, NULL, &E))) {
			std::string buf = "Index: ";
			buf += E.Buffer;
			vsapi->setError(out, buf.c_str());
			return;
		}
		if (FFMS_WriteIndex(CacheFile, Index, &E)) {
			FFMS_DestroyIndex(Index);
			std::string buf = "Index: ";
			buf += E.Buffer;
			vsapi->setError(out, buf.c_str());
			return;
		}
		FFMS_DestroyIndex(Index);
		if (!OverWrite)
			vsapi->propSetData(out, "result", "Index generated", 0, 0);
		else
			vsapi->propSetData(out, "result", "Index generated (forced overwrite)", 0, 0);
	} else {
		FFMS_DestroyIndex(Index);
		vsapi->propSetData(out, "result", "Valid index already exists", 0, 0);
	}
}

static void VS_CC CreateSource(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)  {
	FFMS_Init(0,  1);

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);
	int err;

	const char *Source = vsapi->propGetData(in, "source", 0, 0);
	int Track = (int)vsapi->propGetInt(in, "track", 0, &err);
	if (err)
		Track = -1;
	bool Cache = !!vsapi->propGetInt(in, "cache", 0, &err);
	if (err)
		Cache = true;
	const char *CacheFile = vsapi->propGetData(in, "cachefile", 0, &err);
	int FPSNum = (int)vsapi->propGetInt(in, "fpsnum", 0, &err);
	if (err)
		FPSNum = -1;
	int FPSDen = (int)vsapi->propGetInt(in, "fpsden", 0, &err);
	if (err)
		FPSDen = 1;
	int Threads = (int)vsapi->propGetInt(in, "threads", 0, &err);
	const char *Timecodes = vsapi->propGetData(in, "timecodes", 0, &err);
	int SeekMode = (int)vsapi->propGetInt(in, "seekmode", 0, &err);
	if (err)
		SeekMode = FFMS_SEEK_NORMAL;
	int RFFMode = (int)vsapi->propGetInt(in, "rffmode", 0, &err);
	int Width = (int)vsapi->propGetInt(in, "width", 0, &err);
	int Height = (int)vsapi->propGetInt(in, "height", 0, &err);
	const char *Resizer = vsapi->propGetData(in, "resizer", 0, &err);
	if (err)
		Resizer = "BICUBIC";
	int Format = (int)vsapi->propGetInt(in, "format", 0, &err);

	if (FPSDen < 1) {
		vsapi->setError(out, "Source: FPS denominator needs to be 1 or higher");
		return;
	}

	if (Track <= -2) {
		vsapi->setError(out, "Source: No video track selected");
		return;
	}

	if (SeekMode < -1 || SeekMode > 3) {
		vsapi->setError(out, "Source: Invalid seekmode selected");
		return;
	}

	if (RFFMode < 0 || RFFMode > 2) {
		vsapi->setError(out, "Source: Invalid RFF mode selected");
		return;
	}

	if (RFFMode > 0 && FPSNum > 0) {
		vsapi->setError(out, "Source: RFF modes may not be combined with CFR conversion");
		return;
	}

	if (Timecodes && !_stricmp(Source, Timecodes)) {
		vsapi->setError(out, "Source: Timecodes will overwrite the source");
		return;
	}

	FFMS_Index *Index = NULL;
	std::string DefaultCache;
	if (Cache) {
		if (CacheFile && *CacheFile) {
			if (!_stricmp(Source, CacheFile)) {
				vsapi->setError(out, "Source: Cache will overwrite the source");
				return;
			}
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
		if (!(Index = FFMS_MakeIndex(Source, 0, 0, NULL, NULL, true, NULL, NULL, &E))) {
			std::string buf = "Source: ";
			buf += E.Buffer;
			vsapi->setError(out, buf.c_str());
			return;
		}

		if (Cache)
			if (FFMS_WriteIndex(CacheFile, Index, &E)) {
				FFMS_DestroyIndex(Index);
				std::string buf = "Source: ";
				buf += E.Buffer;
				vsapi->setError(out, buf.c_str());
				return;
			}
	}

	if (Track == -1)
		Track = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_VIDEO, &E);
	if (Track < 0) {
		vsapi->setError(out, "Source: No video track found");
		return;
	}

	if (Timecodes && strcmp(Timecodes, "")) {
		if (FFMS_WriteTimecodes(FFMS_GetTrackFromIndex(Index, Track), Timecodes, &E)) {
			FFMS_DestroyIndex(Index);
			std::string buf = "Source: ";
			buf += E.Buffer;
			vsapi->setError(out, buf.c_str());
			return;
		}
	}

	VSVideoSource *vs;
	try {
		vs = new VSVideoSource(Source, Track, Index, FPSNum, FPSDen, Threads, SeekMode, RFFMode, Width, Height, Resizer, Format, vsapi, core);
	} catch (std::exception &e) {
		FFMS_DestroyIndex(Index);
		vsapi->setError(out, e.what());
		return;
	}

	const VSNodeRef *node = vsapi->createFilter(in, out, "Source", VSVideoSource::Init, VSVideoSource::GetFrame, VSVideoSource::Free, fmSerial, 0,vs, core);
	vsapi->propSetNode(out, "clip", node, 0);

	FFMS_DestroyIndex(Index);
}

static void VS_CC GetLogLevel(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	FFMS_SetLogLevel((int)vsapi->propGetInt(in, "level", 0, 0));
}

static void VS_CC SetLogLevel(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	FFMS_SetLogLevel((int)vsapi->propGetInt(in, "level", 0, 0));
	vsapi->propSetInt(out, "level", FFMS_GetLogLevel(), 0);
}

static void VS_CC GetVersion(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	int Version = FFMS_GetVersion();
	char buf[100];
	sprintf(buf, "%d.%d.%d.%d", Version >> 24, (Version & 0xFF0000) >> 16, (Version & 0xFF00) >> 8, Version & 0xFF);
	vsapi->propSetData(out, "Version", buf, 0, 0);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.vapoursynth.ffms2", "ffms2", "FFmpegSource 2 for VapourSynth", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("Index", "source:data;cachefile:data:opt;indexmask:int:opt;dumpmask:int:opt;audiofile:data:opt;errorhandling:int:opt;overwrite:int:opt;demuxer:data:opt;", CreateIndex, NULL, plugin);
	registerFunc("Source", "source:data;track:int:opt;cache:int:opt;cachefile:data:opt;fpsnum:int:opt;fpsden:int:opt;threads:int:opt;timecodes:data:opt;seekmode:int:opt;width:int:opt;height:int:opt;resizer:data:opt;format:int:opt;", CreateSource, NULL, plugin);
	registerFunc("GetLogLevel", "", GetLogLevel, NULL, plugin);
	registerFunc("SetLogLevel", "level:int;", SetLogLevel, NULL, plugin);
	registerFunc("Version", "", GetVersion, NULL, plugin);
}
