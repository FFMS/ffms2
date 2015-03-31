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

// avcodec.h includes audioconvert.h, but we need to include audioconvert.h
// ourselves later
#define FF_API_OLD_AUDIOCONVERT 0
#define AVUTIL_AUDIOCONVERT_H

#include "utils.h"

#include "codectype.h"
#include "indexing.h"
#include "track.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif // _WIN32

#include <algorithm>
#include <sstream>

extern bool GlobalUseUTF8Paths;

FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const char *Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) { }
FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const std::string &Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) { }

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

void InitNullPacket(AVPacket &pkt) {
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
}

extern "C" {
#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 52, 2, 0, 52, 6, 100)
#include <libavutil/channel_layout.h>
#else
#undef AVUTIL_AUDIOCONVERT_H

// Whether or not av_get_default_channel_layout exists in a given version
// depends on which branch that version is from, since FFmpeg doesn't
// understand the concept of version numbers. Work around this by always using
// our copy, since that's less effort than detecting whether or not it's
// available.
#define av_get_default_channel_layout av_get_default_channel_layout_hurr
#include "libavutil/audioconvert.h"
#undef av_get_default_channel_layout

static int64_t av_get_default_channel_layout(int nb_channels) {
	switch(nb_channels) {
		case 1: return AV_CH_LAYOUT_MONO;
		case 2: return AV_CH_LAYOUT_STEREO;
		case 3: return AV_CH_LAYOUT_SURROUND;
		case 4: return AV_CH_LAYOUT_QUAD;
		case 5: return AV_CH_LAYOUT_5POINT0;
		case 6: return AV_CH_LAYOUT_5POINT1;
		case 7: return AV_CH_LAYOUT_6POINT1;
		case 8: return AV_CH_LAYOUT_7POINT1;
		default: return 0;
	}
}
#endif
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
	}

	if (AP.ChannelLayout == 0)
		AP.ChannelLayout = av_get_default_channel_layout(AP.Channels);
}

// All this filename chikanery that follows is supposed to make sure both local
// codepage (used by avisynth etc) and UTF8 (potentially used by API users) strings
// work correctly on Win32.
// It's a really ugly hack, and I blame Microsoft for it.
#ifdef _WIN32
static std::wstring char_to_wstring(const char *s, unsigned int cp) {
	std::wstring ret;
	int len;
	if (!(len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1, NULL, 0)))
		return ret;

	ret.resize(len);
	if (MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1 , &ret[0], len) <= 0)
		return ret;

	return ret;
}

std::wstring widen_path(const char *s) {
	return char_to_wstring(s, GlobalUseUTF8Paths ? CP_UTF8 : CP_ACP);
}
#endif

// End of filename hackery.

void LAVFOpenFile(const char *SourceFile, AVFormatContext *&FormatContext) {
	if (avformat_open_input(&FormatContext, SourceFile, NULL, NULL) != 0)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Couldn't open '") + SourceFile + "'");

	if (avformat_find_stream_info(FormatContext,NULL) < 0) {
		avformat_close_input(&FormatContext);
		FormatContext = NULL;
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Couldn't find stream information");
	}
}

void FlushBuffers(AVCodecContext *CodecContext) {
	if (CodecContext->codec->flush)
		avcodec_flush_buffers(CodecContext);
	else {
		// If the codec doesn't have flush(), it might not need it... or it
		// might need it and just not implement it as in the case of VC-1, so
		// close and reopen the codec
		const AVCodec *codec = CodecContext->codec;
		avcodec_close(CodecContext);
		// Whether or not codec is const varies between versions
		if (avcodec_open2(CodecContext, const_cast<AVCodec *>(codec), 0) < 0)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_CODEC,
				"Couldn't re-open codec.");
	}
}
