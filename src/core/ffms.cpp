//  Copyright (c) 2007-2011 Fredrik Mellbin
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

#include "audiosource.h"
#include "indexing.h"
#include "videosource.h"
#include "videoutils.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

#include <sstream>
#include <iomanip>

#ifdef FFMS_WIN_DEBUG
#	include <windows.h>
#endif

static bool FFmpegInited = false;
bool GlobalUseUTF8Paths = false;

#ifdef FFMS_WIN_DEBUG

void av_log_windebug_callback(void* ptr, int level, const char* fmt, va_list vl) {
	if (level > av_log_get_level())
		return;

	static int print_prefix=1;
	static int count;
	static char line[1024] = {0}, prev[1024] = {0};
	auto avc = ptr ? *static_cast<AVClass **>(ptr) : nullptr;

	int written = 0;
	if (print_prefix && avc) {
		written = snprintf(line, sizeof(line), "[%s @ %p]", avc->item_name(ptr), ptr);
	}

	written += vsnprintf(line + written, sizeof(line) - written, fmt, vl);

	print_prefix = line[written-1] == '\n';
	line[sizeof(line) - 1] = 0;
	if (print_prefix && !strcmp(line, prev)) {
		count++;
		return;
	}
	if (count > 0) {
		std::stringstream ss;
		ss << "    Last message repeated " << count << " times\n";
		OutputDebugStringA(ss.str().c_str());
		count = 0;
	}
	OutputDebugStringA(line);
	strcpy(prev, line);
}

#endif

FFMS_API(void) FFMS_Init(int, int UseUTF8Paths) {
	if (!FFmpegInited) {
		av_register_all();
		avformat_network_init();
		RegisterCustomParsers();
#ifdef _WIN32
		GlobalUseUTF8Paths = !!UseUTF8Paths;
#else
		(void)UseUTF8Paths;
#endif
#ifdef FFMS_WIN_DEBUG
		av_log_set_callback(av_log_windebug_callback);
		av_log_set_level(AV_LOG_INFO);
#else
		av_log_set_level(AV_LOG_QUIET);
#endif
		FFmpegInited = true;
	}
}

FFMS_API(int) FFMS_GetVersion() {
	return FFMS_VERSION;
}

FFMS_API(int) FFMS_GetLogLevel() {
	return av_log_get_level();
}

FFMS_API(void) FFMS_SetLogLevel(int Level) {
	av_log_set_level(Level);
}

FFMS_API(FFMS_VideoSource *) FFMS_CreateVideoSource(const char *SourceFile, int Track, FFMS_Index *Index, int Threads, int SeekMode, FFMS_ErrorInfo *ErrorInfo) {
	try {
		switch (Index->Decoder) {
			case FFMS_SOURCE_LAVF:
				return CreateLavfVideoSource(SourceFile, Track, *Index, Threads, SeekMode);
			default:
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Unsupported format");
		}
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(FFMS_AudioSource *) FFMS_CreateAudioSource(const char *SourceFile, int Track, FFMS_Index *Index, int DelayMode, FFMS_ErrorInfo *ErrorInfo) {
	try {
		switch (Index->Decoder) {
			case FFMS_SOURCE_LAVF:
				return CreateLavfAudioSource(SourceFile, Track, *Index, DelayMode);
			default:
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Unsupported format");
		}
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(void) FFMS_DestroyVideoSource(FFMS_VideoSource *V) {
	delete V;
}

FFMS_API(void) FFMS_DestroyAudioSource(FFMS_AudioSource *A) {
	delete A;
}

FFMS_API(const FFMS_VideoProperties *) FFMS_GetVideoProperties(FFMS_VideoSource *V) {
	return &V->GetVideoProperties();
}

FFMS_API(const FFMS_AudioProperties *) FFMS_GetAudioProperties(FFMS_AudioSource *A) {
	return &A->GetAudioProperties();
}

FFMS_API(const FFMS_Frame *) FFMS_GetFrame(FFMS_VideoSource *V, int n, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		return V->GetFrame(n);
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(const FFMS_Frame *) FFMS_GetFrameByTime(FFMS_VideoSource *V, double Time, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		return V->GetFrameByTime(Time);
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(int) FFMS_GetAudio(FFMS_AudioSource *A, void *Buf, int64_t Start, int64_t Count, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		A->GetAudio(Buf, Start, Count);
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(int) FFMS_SetOutputFormatV2(FFMS_VideoSource *V, const int *TargetFormats, int Width, int Height, int Resizer, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		V->SetOutputFormat(reinterpret_cast<const PixelFormat *>(TargetFormats), Width, Height, Resizer);
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(void) FFMS_ResetOutputFormatV(FFMS_VideoSource *V) {
	V->ResetOutputFormat();
}

FFMS_API(int) FFMS_SetInputFormatV(FFMS_VideoSource *V, int ColorSpace, int ColorRange, int Format, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		V->SetInputFormat(ColorSpace, ColorRange, static_cast<PixelFormat>(Format));
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(void) FFMS_ResetInputFormatV(FFMS_VideoSource *V) {
	V->ResetInputFormat();
}

FFMS_API(FFMS_ResampleOptions *) FFMS_CreateResampleOptions(FFMS_AudioSource *A) {
	return A->CreateResampleOptions().release();
}

FFMS_API(void) FFMS_DestroyResampleOptions(FFMS_ResampleOptions *options) {
	delete options;
}

FFMS_API(int) FFMS_SetOutputFormatA(FFMS_AudioSource *A, const FFMS_ResampleOptions *options, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		A->SetOutputFormat(*options);
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(void) FFMS_DestroyIndex(FFMS_Index *Index) {
	if (Index)
		Index->Release();
}

FFMS_API(int) FFMS_GetSourceType(FFMS_Index *Index) {
	return Index->Decoder;
}

FFMS_API(FFMS_IndexErrorHandling) FFMS_GetErrorHandling(FFMS_Index *Index) {
	return static_cast<FFMS_IndexErrorHandling>(Index->ErrorHandling);
}

FFMS_API(int) FFMS_GetFirstTrackOfType(FFMS_Index *Index, int TrackType, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	for (int i = 0; i < static_cast<int>(Index->size()); i++)
		if ((*Index)[i].TT == TrackType)
			return i;

	try {
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"No suitable, indexed track found");
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return -1;
	}
}

FFMS_API(int) FFMS_GetFirstIndexedTrackOfType(FFMS_Index *Index, int TrackType, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	for (int i = 0; i < static_cast<int>(Index->size()); i++)
		if ((*Index)[i].TT == TrackType && !(*Index)[i].empty())
			return i;
	try {
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"No suitable, indexed track found");
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return -1;
	}
}

FFMS_API(int) FFMS_GetNumTracks(FFMS_Index *Index) {
	return Index->size();
}

FFMS_API(int) FFMS_GetNumTracksI(FFMS_Indexer *Indexer) {
	return Indexer->GetNumberOfTracks();
}

FFMS_API(int) FFMS_GetTrackType(FFMS_Track *T) {
	return T->TT;
}

FFMS_API(int) FFMS_GetTrackTypeI(FFMS_Indexer *Indexer, int Track) {
	return Indexer->GetTrackType(Track);
}

FFMS_API(const char *) FFMS_GetCodecNameI(FFMS_Indexer *Indexer, int Track) {
	return Indexer->GetTrackCodec(Track);
}

FFMS_API(int) FFMS_GetNumFrames(FFMS_Track *T) {
	return T->VisibleFrameCount();
}

FFMS_API(const FFMS_FrameInfo *) FFMS_GetFrameInfo(FFMS_Track *T, int Frame) {
	return T->GetFrameInfo(static_cast<size_t>(Frame));
}

FFMS_API(FFMS_Track *) FFMS_GetTrackFromIndex(FFMS_Index *Index, int Track) {
	return &(*Index)[Track];
}

FFMS_API(FFMS_Track *) FFMS_GetTrackFromVideo(FFMS_VideoSource *V) {
	return V->GetTrack();
}

FFMS_API(FFMS_Track *) FFMS_GetTrackFromAudio(FFMS_AudioSource *A) {
	return A->GetTrack();
}

FFMS_API(const FFMS_TrackTimeBase *) FFMS_GetTimeBase(FFMS_Track *T) {
	return &T->TB;
}

FFMS_API(int) FFMS_WriteTimecodes(FFMS_Track *T, const char *TimecodeFile, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		T->WriteTimecodes(TimecodeFile);
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(FFMS_Index *) FFMS_MakeIndex(const char *SourceFile, int IndexMask, int DumpMask, TAudioNameCallback ANC, void *ANCPrivate, int ErrorHandling, TIndexCallback IC, void *ICPrivate, FFMS_ErrorInfo *ErrorInfo) {
	FFMS_Indexer *Indexer = FFMS_CreateIndexer(SourceFile, ErrorInfo);
	if (!Indexer)
		return nullptr;
	return FFMS_DoIndexing(Indexer, IndexMask, DumpMask, ANC, ANCPrivate, ErrorHandling, IC, ICPrivate, ErrorInfo);
}

/* Used by FFMS_DefaultAudioFilename */

static std::string IntToStr(int i, int zp = 0) {
	std::stringstream s;
	s.fill('0');
	s.width(zp);
	s << i;
	return s.str();
}

static void ReplaceString(std::string &s, const char *from, std::string const& to) {
	std::string::size_type idx;
	while ((idx = s.find(from)) != std::string::npos)
		s.replace(idx, strlen(from), to);
}

FFMS_API(int) FFMS_DefaultAudioFilename(const char *SourceFile, int Track, const FFMS_AudioProperties *AP, char *FileName, int, void *Private) {
	std::string s = static_cast<char *>(Private);

	ReplaceString(s, "%sourcefile%", SourceFile);
	ReplaceString(s, "%trackn%", std::to_string(Track));
	ReplaceString(s, "%trackzn%", IntToStr(Track, 2));
	ReplaceString(s, "%samplerate%", std::to_string(AP->SampleRate));
	ReplaceString(s, "%channels%", std::to_string(AP->Channels));
	ReplaceString(s, "%bps%", std::to_string(AP->BitsPerSample));
	ReplaceString(s, "%delay%", std::to_string(static_cast<int>(AP->FirstTime)));

	if (FileName)
		strcpy(FileName, s.c_str());

	return s.length() + 1;
}

FFMS_API(FFMS_Indexer *) FFMS_CreateIndexer(const char *SourceFile, FFMS_ErrorInfo *ErrorInfo) {
	return FFMS_CreateIndexerWithDemuxer(SourceFile, FFMS_SOURCE_DEFAULT, ErrorInfo);
}

FFMS_API(FFMS_Indexer *) FFMS_CreateIndexerWithDemuxer(const char *SourceFile, int Demuxer, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		return CreateIndexer(SourceFile, static_cast<FFMS_Sources>(Demuxer));
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(FFMS_Index *) FFMS_DoIndexing(FFMS_Indexer *Indexer, int IndexMask, int DumpMask, TAudioNameCallback ANC, void *ANCPrivate, int ErrorHandling, TIndexCallback IC, void *ICPrivate, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);

	Indexer->SetIndexMask(IndexMask | DumpMask);
	Indexer->SetDumpMask(DumpMask);
	Indexer->SetErrorHandling(ErrorHandling);
	Indexer->SetProgressCallback(IC, ICPrivate);
	Indexer->SetAudioNameCallback(ANC, ANCPrivate);

	FFMS_Index *Index = nullptr;
	try {
		Index = Indexer->DoIndexing();
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
	}
	delete Indexer;
	return Index;
}

FFMS_API(void) FFMS_CancelIndexing(FFMS_Indexer *Indexer) {
	delete Indexer;
}

FFMS_API(FFMS_Index *) FFMS_ReadIndex(const char *IndexFile, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		return new FFMS_Index(IndexFile);
	} catch (FFMS_Exception &e) {
		e.CopyOut(ErrorInfo);
		return nullptr;
	}
}

FFMS_API(int) FFMS_IndexBelongsToFile(FFMS_Index *Index, const char *SourceFile, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		if (!Index->CompareFileSignature(SourceFile))
			throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
				"The index does not belong to the file");
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(int) FFMS_WriteIndex(const char *IndexFile, FFMS_Index *Index, FFMS_ErrorInfo *ErrorInfo) {
	ClearErrorInfo(ErrorInfo);
	try {
		Index->WriteIndex(IndexFile);
	} catch (FFMS_Exception &e) {
		return e.CopyOut(ErrorInfo);
	}
	return FFMS_ERROR_SUCCESS;
}

FFMS_API(int) FFMS_GetPixFmt(const char *Name) {
	return av_get_pix_fmt(Name);
}


FFMS_API(int) FFMS_GetPresentSources() {
	return FFMS_SOURCE_LAVF;
}

FFMS_API(int) FFMS_GetEnabledSources() {
	if (!FFmpegInited)
		return 0;
	return FFMS_SOURCE_LAVF;
}

FFMS_API(const char *) FFMS_GetFormatNameI(FFMS_Indexer *Indexer) {
	return Indexer->GetFormatName();
}

FFMS_API(int) FFMS_GetSourceTypeI(FFMS_Indexer *Indexer) {
	return Indexer->GetSourceType();
}
