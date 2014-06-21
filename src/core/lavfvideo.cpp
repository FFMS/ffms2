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

#include "videosource.h"

namespace {
class FFLAVFVideo : public FFMS_VideoSource {
	AVFormatContext *FormatContext;
	int SeekMode;
	FFSourceResources<FFMS_VideoSource> Res;
	bool SeekByPos;
	int PosOffset;

	void DecodeNextFrame(int64_t *PTS, int64_t *Pos);
	bool SeekTo(int n, int SeekOffset);
	void Free(bool CloseCodec);

	int Seek(int n) {
		int ret = -1;
		if (!SeekByPos || Frames[n].FilePos < 0) {
			ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].PTS, AVSEEK_FLAG_BACKWARD);
			if (ret >= 0)
				return ret;
		}

		if (Frames[n].FilePos >= 0) {
			ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].FilePos + PosOffset, AVSEEK_FLAG_BYTE);
			if (ret >= 0)
				SeekByPos = true;
		}
		return ret;
	}

	int ReadFrame(AVPacket *pkt) {
		int ret = av_read_frame(FormatContext, pkt);
		if (ret >= 0 || ret == AVERROR(EOF)) return ret;

		// Lavf reports the beginning of the actual video data as the packet's
		// position, but the reader requires the header, so we end up seeking
		// to the wrong position. Wait until a read actual fails to adjust the
		// seek targets, so that if this ever gets fixed upstream our workaround
		// doesn't re-break it.
		if (strcmp(FormatContext->iformat->name, "yuv4mpegpipe") == 0) {
			PosOffset = -6;
			Seek(CurrentFrame);
			return av_read_frame(FormatContext, pkt);
		}
		return ret;
	}

public:
	FFLAVFVideo(const char *SourceFile, int Track, FFMS_Index &Index, int Threads, int SeekMode);
	FFMS_Frame *GetFrame(int n);
};

void FFLAVFVideo::Free(bool CloseCodec) {
	if (CloseCodec)
		avcodec_close(CodecContext);
	avformat_close_input(&FormatContext);
}

FFLAVFVideo::FFLAVFVideo(const char *SourceFile, int Track, FFMS_Index &Index,
	int Threads, int SeekMode)
: FFMS_VideoSource(SourceFile, Index, Track, Threads)
, FormatContext(NULL)
, SeekMode(SeekMode)
, Res(this)
, SeekByPos(false)
, PosOffset(0)
{
	LAVFOpenFile(SourceFile, FormatContext);

	if (SeekMode >= 0 && Frames.size() > 1 && Seek(0) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Video track is unseekable");

	CodecContext = FormatContext->streams[VideoTrack]->codec;
	CodecContext->thread_count = DecodingThreads;
	CodecContext->has_b_frames = Frames.MaxBFrames;

	AVCodec *Codec = avcodec_find_decoder(CodecContext->codec_id);
	if (Codec == NULL)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Video codec not found");

	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open video codec");

	Res.CloseCodec(true);

	// Always try to decode a frame to make sure all required parameters are known
	int64_t DummyPTS = 0, DummyPos = 0;
	DecodeNextFrame(&DummyPTS, &DummyPos);

	//VP.image_type = VideoInfo::IT_TFF;
	VP.FPSDenominator = FormatContext->streams[VideoTrack]->time_base.num;
	VP.FPSNumerator = FormatContext->streams[VideoTrack]->time_base.den;

	// sanity check framerate
	if (VP.FPSDenominator > VP.FPSNumerator || VP.FPSDenominator <= 0 || VP.FPSNumerator <= 0) {
		VP.FPSDenominator = 1;
		VP.FPSNumerator = 30;
	}

	// Calculate the average framerate
	if (Frames.size() >= 2) {
		double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
		double TD = (double)(Frames.TB.Den);
		double TN = (double)(Frames.TB.Num);
		VP.FPSDenominator = (unsigned int)(PTSDiff * TN/TD * 1000.0 / (Frames.size() - 1));
		VP.FPSNumerator = 1000000;
	}

	// Set the video properties from the codec context
	SetVideoProperties();

	// Set the SAR from the container if the codec SAR is invalid
	if (VP.SARNum <= 0 || VP.SARDen <= 0) {
		VP.SARNum = FormatContext->streams[VideoTrack]->sample_aspect_ratio.num;
		VP.SARDen = FormatContext->streams[VideoTrack]->sample_aspect_ratio.den;
	}

	// Cannot "output" to PPFrame without doing all other initialization
	// This is the additional mess required for seekmode=-1 to work in a reasonable way
	OutputFrame(DecodeFrame);
}

void FFLAVFVideo::DecodeNextFrame(int64_t *AStartTime, int64_t *Pos) {
	*AStartTime = -1;
	if (HasPendingDelayedFrames()) return;

	AVPacket Packet;
	InitNullPacket(Packet);

	while (ReadFrame(&Packet) >= 0) {
		if (Packet.stream_index != VideoTrack) {
			av_free_packet(&Packet);
			continue;
		}

		if (*AStartTime < 0)
			*AStartTime = Frames.UseDTS ? Packet.dts : Packet.pts;

		if (*Pos < 0)
			*Pos = Packet.pos;

		bool FrameFinished = DecodePacket(&Packet);
		av_free_packet(&Packet);
		if (FrameFinished) return;
	}

	FlushFinalFrames();
}

bool FFLAVFVideo::SeekTo(int n, int SeekOffset) {
	if (SeekMode >= 0) {
		int TargetFrame = n + SeekOffset;
		if (TargetFrame < 0)
			throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_UNKNOWN,
			"Frame accurate seeking is not possible in this file");

		if (SeekMode < 3)
			TargetFrame = Frames.FindClosestVideoKeyFrame(TargetFrame);

		if (SeekMode == 0) {
			if (n < CurrentFrame) {
				Seek(0);
				FlushBuffers(CodecContext);
				CurrentFrame = 0;
				DelayCounter = 0;
				InitialDecode = 1;
			}
		} else {
			// 10 frames is used as a margin to prevent excessive seeking since the predicted best keyframe isn't always selected by avformat
			if (n < CurrentFrame || TargetFrame > CurrentFrame + 10 || (SeekMode == 3 && n > CurrentFrame + 10)) {
				Seek(TargetFrame);
				FlushBuffers(CodecContext);
				DelayCounter = 0;
				InitialDecode = 1;
				return true;
			}
		}
	} else if (n < CurrentFrame) {
		throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_INVALID_ARGUMENT,
			"Non-linear access attempted");
	}
	return false;
}

FFMS_Frame *FFLAVFVideo::GetFrame(int n) {
	GetFrameCheck(n);
	n = Frames.RealFrameNumber(n);

	if (LastFrameNum == n)
		return &LocalFrame;

	int SeekOffset = 0;
	bool Seek = true;

	do {
		bool HasSeeked = false;
		if (Seek) {
			HasSeeked = SeekTo(n, SeekOffset);
			Seek = false;
		}

		if (CurrentFrame + FFMS_CALCULATE_DELAY * CodecContext->ticks_per_frame >= n || HasSeeked)
			CodecContext->skip_frame = AVDISCARD_DEFAULT;
		else
			CodecContext->skip_frame = AVDISCARD_NONREF;

		int64_t StartTime = ffms_av_nopts_value, FilePos = -1;
		DecodeNextFrame(&StartTime, &FilePos);

		if (!HasSeeked)
			continue;

		if (StartTime == ffms_av_nopts_value && !Frames.HasTS) {
			if (FilePos >= 0) {
				CurrentFrame = Frames.FrameFromPos(FilePos);
				if (CurrentFrame >= 0)
					continue;
			}
			// If the track doesn't have timestamps or file positions then
			// just trust that we got to the right place, since we have no
			// way to tell where we are
			else {
				CurrentFrame = n;
				continue;
			}
		}

		CurrentFrame = Frames.FrameFromPTS(StartTime);

		// Is the seek destination time known? Does it belong to a frame?
		if (CurrentFrame < 0) {
			if (SeekMode == 1 || StartTime < 0) {
				// No idea where we are so go back a bit further
				SeekOffset -= 10;
				Seek = true;
				continue;
			}
			CurrentFrame = Frames.ClosestFrameFromPTS(StartTime);
		}

		// We want to know the frame number that we just got out of the decoder,
		// but what we currently know is the frame number of the first packet
		// we fed into the decoder, and these can be different with open-gop or
		// aggressive (non-keyframe) seeking.
		int64_t Pos = Frames[CurrentFrame].FilePos;
		if (CurrentFrame > 0 && Pos != -1) {
			int Prev = CurrentFrame - 1;
			while (Prev >= 0 && Frames[Prev].FilePos != -1 && Frames[Prev].FilePos > Pos)
				--Prev;
			CurrentFrame = Prev + 1;
		}
	} while (++CurrentFrame <= n);

	LastFrameNum = n;
	return OutputFrame(DecodeFrame);
}
}

FFMS_VideoSource *CreateLavfVideoSource(const char *SourceFile, int Track, FFMS_Index &Index, int Threads, int SeekMode) {
	return new FFLAVFVideo(SourceFile, Track, Index, Threads, SeekMode);
}
