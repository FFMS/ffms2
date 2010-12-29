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
{
	LAVFOpenFile(SourceFile, FormatContext);

	CodecContext = FormatContext->streams[TrackNumber]->codec;
	assert(CodecContext);

	AVCodec *Codec = avcodec_find_decoder(CodecContext->codec_id);
	try {
		if (!Codec)
			throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
				"Audio codec not found");

		if (avcodec_open(CodecContext, Codec) < 0)
			throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
				"Could not open audio codec");
	}
	catch (...) {
		av_close_input_file(FormatContext);
		CodecContext = NULL;
		throw;
	}

	// Theoretically seeking by byte offset could work even for files with no
	// timestamps, but the only format I've encountered that doesn't have any
	// timestamps (flac) isn't seekable in any way
	// https://roundup.ffmpeg.org/issue1150
	if (Frames.back().PTS == ffms_av_nopts_value)
		SeekOffset = -1;
	else
		SeekOffset = 10;
	Init(Index, DelayMode);
}

FFLAVFAudio::~FFLAVFAudio() {
	av_close_input_file(FormatContext);
	CodecContext = NULL;
}

void FFLAVFAudio::Seek() {
	// Packets don't always have unique PTSes, so we may not be able to
	// uniquely identify the packet we want. If this is the case, we instead
	// seek to the previous PTS and then decode until the PTS changes, as that's
	// the only time we actually know what packet we're on.
	size_t TargetPacket = PacketNumber;
	if ((PacketNumber > 0 && Frames[PacketNumber].PTS == Frames[PacketNumber - 1].PTS) ||
		(PacketNumber + 1 < Frames.size() && Frames[PacketNumber].PTS == Frames[PacketNumber + 1].PTS)) {
			while (PacketNumber > 0 && Frames[TargetPacket].PTS == Frames[PacketNumber].PTS) --PacketNumber;
	}

	// If PacketNumber == 0 now, we'll end up seeking past our desired packet,
	// but most decoders don't handle seeking back to the beginning very well
	// anyway, so that range is always held in the decoded cache

	if (av_seek_frame(FormatContext, TrackNumber, Frames[PacketNumber].PTS, AVSEEK_FLAG_BACKWARD) < 0)
		av_seek_frame(FormatContext, TrackNumber, Frames[PacketNumber].PTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

	if (TargetPacket != PacketNumber) {
		// Decode until the PTS changes so we know where we are
		int64_t LastPTS = Frames[PacketNumber].PTS;
		while (LastPTS == Frames[PacketNumber].PTS) DecodeNextBlock();
	}
}

bool FFLAVFAudio::ReadPacket(AVPacket *Packet) {
	InitNullPacket(*Packet);

	while (av_read_frame(FormatContext, Packet) >= 0) {
		if (Packet->stream_index == TrackNumber) {
			while (Frames[PacketNumber].PTS < Packet->pts) ++PacketNumber;
			return true;
		}
		av_free_packet(Packet);
	}
	return false;
}
void FFLAVFAudio::FreePacket(AVPacket *Packet) {
	av_free_packet(Packet);
}
