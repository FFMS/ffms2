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

#include "ffms.h"

#include "audiosource.h"
#include "indexing.h"
#include "videosource.h"
#include "videoutils.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

#include <mutex>
#include <sstream>
#include <iomanip>

#ifdef FFMS_WIN_DEBUG
#	include <windows.h>
#endif

static std::once_flag FFmpegOnce;

#ifdef FFMS_WIN_DEBUG

static void av_log_windebug_callback(void* ptr, int level, const char* fmt, va_list vl) {
    if (level > av_log_get_level())
        return;

    static int print_prefix = 1;
    static int count;
    static char line[1024] = {}, prev[1024] = {};
    auto avc = ptr ? *static_cast<AVClass **>(ptr) : nullptr;

    int written = 0;
    if (print_prefix && avc) {
        written = snprintf(line, sizeof(line), "[%s @ %p]", avc->item_name(ptr), ptr);
    }

    written += vsnprintf(line + written, sizeof(line) - written, fmt, vl);

    print_prefix = line[written - 1] == '\n';
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

FFMS_API(void) FFMS_Init(int, int) {
    std::call_once(FFmpegOnce, []() {
#ifdef FFMS_WIN_DEBUG
        av_log_set_callback(av_log_windebug_callback);
        av_log_set_level(AV_LOG_INFO);
#else
        av_log_set_level(AV_LOG_QUIET);
#endif
    });
}

FFMS_API(void) FFMS_Deinit() {

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
        return new FFMS_VideoSource(SourceFile, *Index, Track, Threads, SeekMode);
    } catch (FFMS_Exception &e) {
        e.CopyOut(ErrorInfo);
        return nullptr;
    }
}

FFMS_API(FFMS_AudioSource *) FFMS_CreateAudioSource(const char *SourceFile, int Track, FFMS_Index *Index, int DelayMode, FFMS_ErrorInfo *ErrorInfo) {
    return FFMS_CreateAudioSource2(SourceFile, Track, Index, DelayMode, -1, 0, ErrorInfo);
}

FFMS_API(FFMS_AudioSource *) FFMS_CreateAudioSource2(const char *SourceFile, int Track, FFMS_Index *Index, int DelayMode, int FillGaps, double DrcScale, FFMS_ErrorInfo *ErrorInfo) {
    try {
        return new FFMS_AudioSource(SourceFile, *Index, Track, DelayMode, FillGaps, DrcScale);
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
        V->SetOutputFormat(reinterpret_cast<const AVPixelFormat *>(TargetFormats), Width, Height, Resizer);
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
        V->SetInputFormat(ColorSpace, ColorRange, static_cast<AVPixelFormat>(Format));
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
    delete Index;
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
    return static_cast<int>(Index->size());
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

FFMS_API(FFMS_Indexer *) FFMS_CreateIndexer(const char *SourceFile, FFMS_ErrorInfo *ErrorInfo) {
    return FFMS_CreateIndexer2(SourceFile, nullptr, 0, ErrorInfo);
}

FFMS_API(FFMS_Indexer *) FFMS_CreateIndexer2(const char *SourceFile, const FFMS_KeyValuePair *DemuxerOptions, int NumOptions, FFMS_ErrorInfo *ErrorInfo) {
    ClearErrorInfo(ErrorInfo);
    try {
        return new FFMS_Indexer(SourceFile, DemuxerOptions, NumOptions);
    } catch (FFMS_Exception &e) {
        e.CopyOut(ErrorInfo);
        return nullptr;
    }
}

FFMS_API(FFMS_Index *) FFMS_DoIndexing2(FFMS_Indexer *Indexer, int ErrorHandling, FFMS_ErrorInfo *ErrorInfo) {
    ClearErrorInfo(ErrorInfo);

    Indexer->SetErrorHandling(ErrorHandling);

    FFMS_Index *Index = nullptr;
    try {
        Index = Indexer->DoIndexing();
    } catch (FFMS_Exception &e) {
        e.CopyOut(ErrorInfo);
    }
    delete Indexer;
    return Index;
}

FFMS_API(void) FFMS_TrackIndexSettings(FFMS_Indexer *Indexer, int Track, int Index, int) {
    Indexer->SetIndexTrack(Track, !!Index);
}

FFMS_API(void) FFMS_TrackTypeIndexSettings(FFMS_Indexer *Indexer, int TrackType, int Index, int) {
    Indexer->SetIndexTrackType(TrackType, !!Index);
}

FFMS_API(void) FFMS_SetProgressCallback(FFMS_Indexer *Indexer, TIndexCallback IC, void *ICPrivate) {
    Indexer->SetProgressCallback(IC, ICPrivate);
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

FFMS_API(FFMS_Index *) FFMS_ReadIndexFromBuffer(const uint8_t *Buffer, size_t Size, FFMS_ErrorInfo *ErrorInfo) {
    ClearErrorInfo(ErrorInfo);
    try {
        return new FFMS_Index(Buffer, Size);
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
        Index->WriteIndexFile(IndexFile);
    } catch (FFMS_Exception &e) {
        return e.CopyOut(ErrorInfo);
    }
    return FFMS_ERROR_SUCCESS;
}

FFMS_API(int) FFMS_WriteIndexToBuffer(uint8_t **BufferPtr, size_t *Size, FFMS_Index *Index, FFMS_ErrorInfo *ErrorInfo) {
    ClearErrorInfo(ErrorInfo);
    uint8_t *buf;

    try {
        buf = Index->WriteIndexBuffer(Size);
    } catch (FFMS_Exception &e) {
        *Size = 0;
        *BufferPtr = nullptr;
        return e.CopyOut(ErrorInfo);
    }

    *BufferPtr = buf;

    return FFMS_ERROR_SUCCESS;
}

FFMS_API(void) FFMS_FreeIndexBuffer(uint8_t **BufferPtr) {
    av_freep(BufferPtr);
}

FFMS_API(int) FFMS_GetPixFmt(const char *Name) {
    return av_get_pix_fmt(Name);
}

FFMS_API(const char *) FFMS_GetFormatNameI(FFMS_Indexer *Indexer) {
    return Indexer->GetFormatName();
}
