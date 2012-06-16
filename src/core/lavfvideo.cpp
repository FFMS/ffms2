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
, Res(FFSourceResources<FFMS_VideoSource>(this))
{
	AVCodec *Codec = NULL;

	LAVFOpenFile(SourceFile, FormatContext);

	if (SeekMode >= 0 && Frames.size() > 1 && av_seek_frame(FormatContext, VideoTrack, Frames[0].PTS, AVSEEK_FLAG_BACKWARD) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Video track is unseekable");

	CodecContext = FormatContext->streams[VideoTrack]->codec;
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(52,111,0)
	CodecContext->thread_count = DecodingThreads;
#else
	if (avcodec_thread_init(CodecContext, DecodingThreads))
		CodecContext->thread_count = 1;
#endif

	Codec = avcodec_find_decoder(CodecContext->codec_id);
	if (Codec == NULL)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Video codec not found");

	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open video codec");

	Res.CloseCodec(true);

	// Always try to decode a frame to make sure all required parameters are known
	int64_t DummyPTS, DummyPos;
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
		VP.FPSDenominator = (unsigned int)(((double)1000000) / (double)((Frames.size() - 1) / ((PTSDiff * TN/TD) / (double)1000)));
		VP.FPSNumerator = 1000000;
	}

	// Set the video properties from the codec context
	SetVideoProperties();

	// Cannot "output" to PPFrame without doing all other initialization
	// This is the additional mess required for seekmode=-1 to work in a reasonable way
	OutputFrame(DecodeFrame);
}

void FFLAVFVideo::DecodeNextFrame(int64_t *AStartTime, int64_t *Pos) {
	*AStartTime = -1;
	if (HasPendingDelayedFrames()) return;

	AVPacket Packet;
	InitNullPacket(Packet);

	while (av_read_frame(FormatContext, &Packet) >= 0) {
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
				av_seek_frame(FormatContext, VideoTrack, Frames[0].PTS, AVSEEK_FLAG_BACKWARD);
				FlushBuffers(CodecContext);
				CurrentFrame = 0;
				DelayCounter = 0;
				InitialDecode = 1;
			}
		} else {
			// 10 frames is used as a margin to prevent excessive seeking since the predicted best keyframe isn't always selected by avformat
			if (n < CurrentFrame || TargetFrame > CurrentFrame + 10 || (SeekMode == 3 && n > CurrentFrame + 10)) {
				av_seek_frame(FormatContext, VideoTrack, Frames[TargetFrame].PTS, AVSEEK_FLAG_BACKWARD);
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

		if (CurrentFrame + FFMS_CALCULATE_DELAY >= n || HasSeeked)
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
			}
			else
				CurrentFrame = Frames.ClosestFrameFromPTS(StartTime);
		}
	} while (++CurrentFrame <= n);

	LastFrameNum = n;
	return OutputFrame(DecodeFrame);
}
