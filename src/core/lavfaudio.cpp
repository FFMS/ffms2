//  Copyright (c) 2011 Thomas Goyne <tgoyne@gmail.com>
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

#include "audiosource.h"
#include <cassert>

FFLAVFAudio::FFLAVFAudio(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode)
: FFMS_AudioSource(SourceFile, Index, Track)
, FormatContext(NULL)
, LastValidTS(AV_NOPTS_VALUE)
{
	LAVFOpenFile(SourceFile, FormatContext);

	CodecContext.reset(FormatContext->streams[TrackNumber]->codec);
	assert(CodecContext);

	AVCodec *Codec = avcodec_find_decoder(CodecContext->codec_id);
	try {
		if (!Codec)
			throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
				"Audio codec not found");

		if (avcodec_open2(CodecContext, Codec, NULL) < 0)
			throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
				"Could not open audio codec");
	}
	catch (...) {
		avformat_close_input(&FormatContext);
		throw;
	}

	if (Frames.back().PTS == Frames.front().PTS)
		SeekOffset = -1;
	else
		SeekOffset = 10;
	Init(Index, DelayMode);
}

FFLAVFAudio::~FFLAVFAudio() {
	avformat_close_input(&FormatContext);
}

int64_t FFLAVFAudio::FrameTS(size_t Packet) const {
	return Frames.HasTS ? Frames[Packet].PTS : Frames[Packet].FilePos;
}

void FFLAVFAudio::Seek() {
	size_t TargetPacket = GetSeekablePacketNumber(Frames, PacketNumber);
	LastValidTS = AV_NOPTS_VALUE;

	int Flags = Frames.HasTS ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE;

	if (av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags) < 0)
		av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags | AVSEEK_FLAG_ANY);

	if (TargetPacket != PacketNumber) {
		// Decode until the PTS changes so we know where we are
		int64_t LastPTS = FrameTS(PacketNumber);
		while (LastPTS == FrameTS(PacketNumber)) DecodeNextBlock();
	}
}

bool FFLAVFAudio::ReadPacket(AVPacket *Packet) {
	InitNullPacket(*Packet);

	while (av_read_frame(FormatContext, Packet) >= 0) {
		if (Packet->stream_index == TrackNumber) {
			// Required because not all audio packets, especially in ogg, have a pts. Use the previous valid packet's pts instead.
			if (Packet->pts == AV_NOPTS_VALUE)
				Packet->pts = LastValidTS;
			else
				LastValidTS = Packet->pts;

			// This only happens if a really shitty demuxer seeks to a packet without pts *hrm* ogg *hrm* so read until a valid pts is reached
			int64_t PacketTS = Frames.HasTS ? Packet->pts : Packet->pos;
			if (PacketTS != AV_NOPTS_VALUE) {
				while (PacketNumber > 0 && FrameTS(PacketNumber) > PacketTS) --PacketNumber;
				while (FrameTS(PacketNumber) < PacketTS) ++PacketNumber;
				return true;
			}
		}
		av_free_packet(Packet);
	}
	return false;
}

void FFLAVFAudio::FreePacket(AVPacket *Packet) {
	av_free_packet(Packet);
}
