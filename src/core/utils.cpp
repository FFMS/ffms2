//  Copyright (c) 2007-2015 Fredrik Mellbin
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

extern "C" {
#include <libavutil/channel_layout.h>
}

#include "utils.h"

#include "indexing.h"
#include "track.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif // _WIN32

#include <algorithm>
#include <sstream>

FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const char *Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) {}
FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const std::string &Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) {}

int FFMS_Exception::CopyOut(FFMS_ErrorInfo *ErrorInfo) const {
    if (ErrorInfo) {
        ErrorInfo->ErrorType = _ErrorType;
        ErrorInfo->SubType = _SubType;

        if (ErrorInfo->BufferSize > 0) {
            memset(ErrorInfo->Buffer, 0, ErrorInfo->BufferSize);
            _Message.copy(ErrorInfo->Buffer, ErrorInfo->BufferSize - 1);
        }
    }

    return (_ErrorType << 16) | _SubType;
}

void ClearErrorInfo(FFMS_ErrorInfo *ErrorInfo) {
    if (ErrorInfo) {
        ErrorInfo->ErrorType = FFMS_ERROR_SUCCESS;
        ErrorInfo->SubType = FFMS_ERROR_SUCCESS;

        if (ErrorInfo->BufferSize > 0)
            ErrorInfo->Buffer[0] = 0;
    }
}

void FillAP(FFMS_AudioProperties &AP, AVCodecContext *CTX, FFMS_Track &Frames) {
    AP.SampleFormat = static_cast<FFMS_SampleFormat>(av_get_packed_sample_fmt(CTX->sample_fmt));
    AP.BitsPerSample = av_get_bytes_per_sample(CTX->sample_fmt) * 8;
    AP.Channels = CTX->channels;
    AP.ChannelLayout = CTX->channel_layout;
    AP.SampleRate = CTX->sample_rate;
    if (!Frames.empty()) {
        AP.NumSamples = (Frames.back()).SampleStart + (Frames.back()).SampleCount;
        AP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
        AP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
        AP.LastEndTime = (((Frames.back().PTS + Frames.LastDuration) * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
    }

    if (AP.ChannelLayout == 0)
        AP.ChannelLayout = av_get_default_channel_layout(AP.Channels);
}

void LAVFOpenFile(const char *SourceFile, AVFormatContext *&FormatContext, int Track) {
    if (avformat_open_input(&FormatContext, SourceFile, nullptr, nullptr) != 0)
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("Couldn't open '") + SourceFile + "'");

    if (avformat_find_stream_info(FormatContext, nullptr) < 0) {
        avformat_close_input(&FormatContext);
        FormatContext = nullptr;
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            "Couldn't find stream information");
    }

    for (int i = 0; i < (int)FormatContext->nb_streams; i++)
        if (i != Track)
            FormatContext->streams[i]->discard = AVDISCARD_ALL;
}

int ResizerNameToSWSResizer(const char *ResizerName) {
    if (!ResizerName)
        return 0;
    std::string s = ResizerName;
    std::transform(s.begin(), s.end(), s.begin(), toupper);
    if (s == "FAST_BILINEAR")
        return SWS_FAST_BILINEAR;
    if (s == "BILINEAR")
        return SWS_BILINEAR;
    if (s == "BICUBIC")
        return SWS_BICUBIC;
    if (s == "X")
        return SWS_X;
    if (s == "POINT")
        return SWS_POINT;
    if (s == "AREA")
        return SWS_AREA;
    if (s == "BICUBLIN")
        return SWS_BICUBLIN;
    if (s == "GAUSS")
        return SWS_GAUSS;
    if (s == "SINC")
        return SWS_SINC;
    if (s == "LANCZOS")
        return SWS_LANCZOS;
    if (s == "SPLINE")
        return SWS_SPLINE;
    return 0;
}

bool IsSamePath(const char *p1, const char *p2) {
    // assume windows is the only OS with a case insensitive filesystem and ignore all path complications
#ifndef _WIN32
    return !strcmp(p1, p2);
#else
    return !_stricmp(p1, p2);
#endif
}
