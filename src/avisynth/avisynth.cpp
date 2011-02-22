//  Copyright (c) 2007-2009 Fredrik Mellbin
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
#include "ffswscale.h"
#include "ffpp.h"
#include "avsutils.h"

static int GetNumberOfLogicalCPUs() {
	SYSTEM_INFO SI;
	GetSystemInfo(&SI);
	return SI.dwNumberOfProcessors;
}

static AVSValue __cdecl CreateFFIndex(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	FFMS_Init((int)AvisynthToFFCPUFlags(Env->GetCPUFlags()),  Args[7].AsBool(false));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);


	if (!Args[0].Defined())
    	Env->ThrowError("FFIndex: No source specified");

	const char *Source = Args[0].AsString();
	const char *CacheFile = Args[1].AsString("");
	int IndexMask = Args[2].AsInt(-1);
	int DumpMask = Args[3].AsInt(0);
	const char *AudioFile = Args[4].AsString("%sourcefile%.%trackzn%.w64");
	int ErrorHandling = Args[5].AsInt(FFMS_IEH_IGNORE);
	bool OverWrite = Args[6].AsBool(false);
	const char *DemuxerStr = Args[8].AsString("default");

	std::string DefaultCache(Source);
	DefaultCache.append(".ffindex");
	if (!strcmp(CacheFile, ""))
		CacheFile = DefaultCache.c_str();

	if (!strcmp(AudioFile, ""))
		Env->ThrowError("FFIndex: Specifying an empty audio filename is not allowed");

	int Demuxer;
	if (!strcmp(DemuxerStr, "default"))
		Demuxer = FFMS_SOURCE_DEFAULT;
	else if (!strcmp(DemuxerStr, "lavf"))
		Demuxer = FFMS_SOURCE_LAVF;
	else if (!strcmp(DemuxerStr, "matroska"))
		Demuxer = FFMS_SOURCE_MATROSKA;
	else if (!strcmp(DemuxerStr, "haalimpeg"))
		Demuxer = FFMS_SOURCE_HAALIMPEG;
	else if (!strcmp(DemuxerStr, "haaliogg"))
		Demuxer = FFMS_SOURCE_HAALIOGG;
	else
		Env->ThrowError("FFIndex: Invalid demuxer requested");

	FFMS_Index *Index = NULL;
	if (OverWrite || !(Index = FFMS_ReadIndex(CacheFile, &E))) {
		FFMS_Indexer *Indexer = FFMS_CreateIndexerWithDemuxer(Source, Demuxer, &E);
		if (!Indexer)
			Env->ThrowError("FFIndex: %s", E.Buffer);
		if (!(Index = FFMS_DoIndexing(Indexer, IndexMask, DumpMask, FFMS_DefaultAudioFilename, (void *)AudioFile, ErrorHandling, NULL, NULL, &E)))
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
	FFMS_Init((int)AvisynthToFFCPUFlags(Env->GetCPUFlags()), Args[15].AsBool(false));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	if (!Args[0].Defined())
    	Env->ThrowError("FFVideoSource: No source specified");

	const char *Source = Args[0].AsString();
	int Track = Args[1].AsInt(-1);
	bool Cache = Args[2].AsBool(true);
	const char *CacheFile = Args[3].AsString("");
	int FPSNum = Args[4].AsInt(-1);
	int FPSDen = Args[5].AsInt(1);
	const char *PP = Args[6].AsString("");
	int Threads = Args[7].AsInt(-1);
	const char *Timecodes = Args[8].AsString("");
	int SeekMode = Args[9].AsInt(1);
	int RFFMode = Args[10].AsInt(0);
	int Width = Args[11].AsInt(0);
	int Height = Args[12].AsInt(0);
	const char *Resizer = Args[13].AsString("BICUBIC");
	const char *ColorSpace = Args[14].AsString("");

	if (FPSDen < 1)
		Env->ThrowError("FFVideoSource: FPS denominator needs to be 1 or higher");

	if (Track <= -2)
		Env->ThrowError("FFVideoSource: No video track selected");

	if (SeekMode < -1 || SeekMode > 3)
		Env->ThrowError("FFVideoSource: Invalid seekmode selected");

	if (Threads <= 0)
		Threads = GetNumberOfLogicalCPUs();

	if (RFFMode < 0 || RFFMode > 2)
		Env->ThrowError("FFVideoSource: Invalid RFF mode selected");

	if (RFFMode > 0 && FPSNum > 0)
		Env->ThrowError("FFVideoSource: RFF modes may not be combined with CFR conversion");

	if (!_stricmp(Source, Timecodes))
		Env->ThrowError("FFVideoSource: Timecodes will overwrite the source");

	FFMS_Index *Index = NULL;
	std::string DefaultCache;
	if (Cache) {
		if (*CacheFile) {
			if (!_stricmp(Source, CacheFile))
				Env->ThrowError("FFVideoSource: Cache will overwrite the source");
			Index = FFMS_ReadIndex(CacheFile, &E);
		}
		else {
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
		if (!(Index = FFMS_MakeIndex(Source, 0, 0, NULL, NULL, true, NULL, NULL, &E)))
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

	if (!strcmp(PP, ""))
		PP = NULL;

	AvisynthVideoSource *Filter;

	try {
		Filter = new AvisynthVideoSource(Source, Track, Index, FPSNum, FPSDen, PP, Threads, SeekMode, RFFMode, Width, Height, Resizer, ColorSpace, Env);
	} catch (...) {
		FFMS_DestroyIndex(Index);
		throw;
	}

	FFMS_DestroyIndex(Index);
	return Filter;
}

static AVSValue __cdecl CreateFFAudioSource(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	FFMS_Init((int)AvisynthToFFCPUFlags(Env->GetCPUFlags()), Args[5].AsBool(false));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	if (!Args[0].Defined())
    	Env->ThrowError("FFAudioSource: No source specified");

	const char *Source = Args[0].AsString();
	int Track = Args[1].AsInt(-1);
	bool Cache = Args[2].AsBool(true);
	const char *CacheFile = Args[3].AsString("");
	int AdjustDelay = Args[4].AsInt(-1);

	if (Track <= -2)
		Env->ThrowError("FFAudioSource: No audio track selected");

	FFMS_Index *Index = NULL;
	std::string DefaultCache;
	if (Cache) {
		if (*CacheFile) {
			if (!_stricmp(Source, CacheFile))
				Env->ThrowError("FFAudioSource: Cache will overwrite the source");
			Index = FFMS_ReadIndex(CacheFile, &E);
		}
		else {
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
		Index = NULL;
	}

	// More complicated for finding a default track, reindex the file if at least one audio track exists
	if (Index && FFMS_GetFirstTrackOfType(Index, FFMS_TYPE_AUDIO, &E) >= 0
		&& FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_AUDIO, &E) < 0) {
		for (int i = 0; i < FFMS_GetNumTracks(Index); i++) {
			if (FFMS_GetTrackType(FFMS_GetTrackFromIndex(Index, i)) == FFMS_TYPE_AUDIO) {
				FFMS_DestroyIndex(Index);
				Index = NULL;
				break;
			}
		}
	}

	if (!Index) {
		if (!(Index = FFMS_MakeIndex(Source, -1, 0, NULL, NULL, true, NULL, NULL, &E)))
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
		Filter = new AvisynthAudioSource(Source, Track, Index, AdjustDelay, Env);
	} catch (...) {
		FFMS_DestroyIndex(Index);
		throw;
	}

	FFMS_DestroyIndex(Index);
	return Filter;
}

#ifdef FFMS_USE_POSTPROC
static AVSValue __cdecl CreateFFPP(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	return new FFPP(Args[0].AsClip(), Args[1].AsString(""), Env);
}
#endif // FFMS_USE_POSTPROC

static AVSValue __cdecl CreateSWScale(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	return new SWScale(Args[0].AsClip(), Args[1].AsInt(0), Args[2].AsInt(0), Args[3].AsString("BICUBIC"), Args[4].AsString(""), Env);
}

static AVSValue __cdecl FFGetLogLevel(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	return FFMS_GetLogLevel();
}

static AVSValue __cdecl FFSetLogLevel(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
	FFMS_SetLogLevel(Args[0].AsInt());
	return FFMS_GetLogLevel();
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* Env) {
    Env->AddFunction("FFIndex", "[source]s[cachefile]s[indexmask]i[dumpmask]i[audiofile]s[errorhandling]i[overwrite]b[utf8]b[demuxer]s", CreateFFIndex, 0);
	Env->AddFunction("FFVideoSource", "[source]s[track]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[pp]s[threads]i[timecodes]s[seekmode]i[rffmode]i[width]i[height]i[resizer]s[colorspace]s[utf8]b", CreateFFVideoSource, 0);
    Env->AddFunction("FFAudioSource", "[source]s[track]i[cache]b[cachefile]s[adjustdelay]i[utf8]b", CreateFFAudioSource, 0);
#ifdef FFMS_USE_POSTPROC
	Env->AddFunction("FFPP", "c[pp]s", CreateFFPP, 0);
#endif // FFMS_USE_POSTPROC
	Env->AddFunction("SWScale", "c[width]i[height]i[resizer]s[colorspace]s", CreateSWScale, 0);
	Env->AddFunction("FFGetLogLevel", "", FFGetLogLevel, 0);
	Env->AddFunction("FFSetLogLevel", "i", FFSetLogLevel, 0);

    return "FFmpegSource - The Second Coming V2.0 Final";
}
