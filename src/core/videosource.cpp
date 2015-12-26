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

#include "videosource.h"

#include "indexing.h"
#include "videoutils.h"

#include <algorithm>
#include <thread>

namespace {
void CopyAVFrameFields(AVFrame &Picture, FFMS_Frame &Dst) {
	for (int i = 0; i < 4; i++) {
		Dst.Data[i] = Picture.data[i];
		Dst.Linesize[i] = Picture.linesize[i];
	}
}

// this might look stupid, but we have actually had crashes caused by not checking like this.
void SanityCheckFrameForData(AVFrame *Frame) {
	for (int i = 0; i < 4; i++) {
		if (Frame->data[i] != nullptr && Frame->linesize[i] != 0)
			return;
	}

	throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC, "Insanity detected: decoder returned an empty frame");
}
}

void FFMS_VideoSource::GetFrameCheck(int n) {
	if (n < 0 || n >= VP.NumFrames)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds frame requested");
}

FFMS_Frame *FFMS_VideoSource::OutputFrame(AVFrame *Frame) {
	SanityCheckFrameForData(Frame);

	if (LastFrameWidth != CodecContext->width || LastFrameHeight != CodecContext->height || LastFramePixelFormat != CodecContext->pix_fmt) {
		if (TargetHeight > 0 && TargetWidth > 0 && !TargetPixelFormats.empty()) {
			if (!InputFormatOverridden) {
				InputFormat = FFMS_PIX_FMT(NONE);
				InputColorSpace = AVCOL_SPC_UNSPECIFIED;
				InputColorRange = AVCOL_RANGE_UNSPECIFIED;
			}

			ReAdjustOutputFormat();
		}
	}

	if (SWS) {
		sws_scale(SWS, Frame->data, Frame->linesize, 0, CodecContext->height, SWSFrame.data, SWSFrame.linesize);
		CopyAVFrameFields(SWSFrame, LocalFrame);
	} else {
		// Special case to avoid ugly casts
		for (int i = 0; i < 4; i++) {
			LocalFrame.Data[i] = Frame->data[i];
			LocalFrame.Linesize[i] = Frame->linesize[i];
		}
	}

	LocalFrame.EncodedWidth = CodecContext->width;
	LocalFrame.EncodedHeight = CodecContext->height;
	LocalFrame.EncodedPixelFormat = CodecContext->pix_fmt;
	LocalFrame.ScaledWidth = TargetWidth;
	LocalFrame.ScaledHeight = TargetHeight;
	LocalFrame.ConvertedPixelFormat = OutputFormat;
	LocalFrame.KeyFrame = Frame->key_frame;
	LocalFrame.PictType = av_get_picture_type_char(Frame->pict_type);
	LocalFrame.RepeatPict = Frame->repeat_pict;
	LocalFrame.InterlacedFrame = Frame->interlaced_frame;
	LocalFrame.TopFieldFirst = Frame->top_field_first;
	LocalFrame.ColorSpace = OutputColorSpace;
	LocalFrame.ColorRange = OutputColorRange;
	LocalFrame.ColorPrimaries = CodecContext->color_primaries;
	LocalFrame.TransferCharateristics = CodecContext->color_trc;
	LocalFrame.ChromaLocation = CodecContext->chroma_sample_location;

	LastFrameHeight = CodecContext->height;
	LastFrameWidth = CodecContext->width;
	LastFramePixelFormat = CodecContext->pix_fmt;

	return &LocalFrame;
}

FFMS_VideoSource::FFMS_VideoSource(const char *SourceFile, FFMS_Index &Index, int Track, int Threads)
: Index(Index)
, CodecContext(nullptr)
{
	if (Track < 0 || Track >= static_cast<int>(Index.size()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds track index selected");

	if (Index[Track].TT != FFMS_TYPE_VIDEO)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Not a video track");

	if (Index[Track].empty())
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Video track contains no frames");

	if (!Index.CompareFileSignature(SourceFile))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
			"The index does not match the source file");

	Frames = Index[Track];
	VideoTrack = Track;

	VP = {};
	LocalFrame = {};
	SWS = nullptr;
	LastFrameNum = 0;
	CurrentFrame = 1;
	DelayCounter = 0;
	InitialDecode = 1;

	LastFrameHeight = -1;
	LastFrameWidth = -1;
	LastFramePixelFormat = FFMS_PIX_FMT(NONE);

	TargetHeight = -1;
	TargetWidth = -1;
	TargetResizer = 0;

	OutputFormat = FFMS_PIX_FMT(NONE);
	OutputColorSpace = AVCOL_SPC_UNSPECIFIED;
	OutputColorRange = AVCOL_RANGE_UNSPECIFIED;

	InputFormatOverridden = false;
	InputFormat = FFMS_PIX_FMT(NONE);
	InputColorSpace = AVCOL_SPC_UNSPECIFIED;
	InputColorRange = AVCOL_RANGE_UNSPECIFIED;
	if (Threads < 1)
		// libav current has issues with greater than 16 threads
		DecodingThreads = (std::min)(std::thread::hardware_concurrency(), 16u);
	else
		DecodingThreads = Threads;
	DecodeFrame = av_frame_alloc();
	LastDecodedFrame = av_frame_alloc();

	// Dummy allocations so the unallocated case doesn't have to be handled later
	if (av_image_alloc(SWSFrame.data, SWSFrame.linesize, 16, 16, FFMS_PIX_FMT(GRAY8), 1) < 0)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_ALLOCATION_FAILED,
			"Could not allocate dummy frame.");

	Index.AddRef();
}

FFMS_VideoSource::~FFMS_VideoSource() {
	if (SWS)
		sws_freeContext(SWS);

	av_freep(&SWSFrame.data[0]);
	av_freep(&DecodeFrame);
	av_freep(&LastDecodedFrame);

	Index.Release();
}

FFMS_Frame *FFMS_VideoSource::GetFrameByTime(double Time) {
	int Frame = Frames.ClosestFrameFromPTS(static_cast<int64_t>((Time * 1000 * Frames.TB.Den) / Frames.TB.Num));
	return GetFrame(Frame);
}

static AVColorRange handle_jpeg(AVPixelFormat *format) {
	switch (*format) {
		case FFMS_PIX_FMT(YUVJ420P): *format = FFMS_PIX_FMT(YUV420P); return AVCOL_RANGE_JPEG;
		case FFMS_PIX_FMT(YUVJ422P): *format = FFMS_PIX_FMT(YUV422P); return AVCOL_RANGE_JPEG;
		case FFMS_PIX_FMT(YUVJ444P): *format = FFMS_PIX_FMT(YUV444P); return AVCOL_RANGE_JPEG;
		case FFMS_PIX_FMT(YUVJ440P): *format = FFMS_PIX_FMT(YUV440P); return AVCOL_RANGE_JPEG;
		default:                                                      return AVCOL_RANGE_UNSPECIFIED;
	}
}

void FFMS_VideoSource::SetOutputFormat(const AVPixelFormat *TargetFormats, int Width, int Height, int Resizer) {
	TargetWidth = Width;
	TargetHeight = Height;
	TargetResizer = Resizer;
	TargetPixelFormats.clear();
	while (*TargetFormats != FFMS_PIX_FMT(NONE))
		TargetPixelFormats.push_back(*TargetFormats++);
	OutputFormat = FFMS_PIX_FMT(NONE);

	ReAdjustOutputFormat();
	OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetInputFormat(int ColorSpace, int ColorRange, AVPixelFormat Format) {
	InputFormatOverridden = true;

	if (Format != FFMS_PIX_FMT(NONE))
		InputFormat = Format;
	if (ColorRange != AVCOL_RANGE_UNSPECIFIED)
		InputColorRange = (AVColorRange)ColorRange;
	if (ColorSpace != AVCOL_SPC_UNSPECIFIED)
		InputColorSpace = (AVColorSpace)ColorSpace;

	if (TargetPixelFormats.size()) {
		ReAdjustOutputFormat();
		OutputFrame(DecodeFrame);
	}
}

void FFMS_VideoSource::DetectInputFormat() {
	if (InputFormat == FFMS_PIX_FMT(NONE))
		InputFormat = CodecContext->pix_fmt;

	AVColorRange RangeFromFormat = handle_jpeg(&InputFormat);

	if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
		InputColorRange = RangeFromFormat;
	if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
		InputColorRange = CodecContext->color_range;
	if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
		InputColorRange = AVCOL_RANGE_MPEG;

	if (InputColorSpace == AVCOL_SPC_UNSPECIFIED)
		InputColorSpace = CodecContext->colorspace;
}

void FFMS_VideoSource::ReAdjustOutputFormat() {
	if (SWS) {
		sws_freeContext(SWS);
		SWS = nullptr;
	}

	DetectInputFormat();

	OutputFormat = FindBestPixelFormat(TargetPixelFormats, InputFormat);
	if (OutputFormat == FFMS_PIX_FMT(NONE)) {
		ResetOutputFormat();
		throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
			"No suitable output format found");
	}

	OutputColorRange = handle_jpeg(&OutputFormat);
	if (OutputColorRange == AVCOL_RANGE_UNSPECIFIED)
		OutputColorRange = CodecContext->color_range;
	if (OutputColorRange == AVCOL_RANGE_UNSPECIFIED)
		OutputColorRange = InputColorRange;

	OutputColorSpace = CodecContext->colorspace;
	if (OutputColorSpace == AVCOL_SPC_UNSPECIFIED)
		OutputColorSpace = InputColorSpace;

	if (InputFormat != OutputFormat ||
		TargetWidth != CodecContext->width ||
		TargetHeight != CodecContext->height ||
		InputColorSpace != OutputColorSpace ||
		InputColorRange != OutputColorRange)
	{
		SWS = GetSwsContext(
			CodecContext->width, CodecContext->height, InputFormat, InputColorSpace, InputColorRange,
			TargetWidth, TargetHeight, OutputFormat, OutputColorSpace, OutputColorRange,
			TargetResizer);

		if (!SWS) {
			ResetOutputFormat();
			throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
				"Failed to allocate SWScale context");
		}
	}

	av_freep(&SWSFrame.data[0]);
	if (av_image_alloc(SWSFrame.data, SWSFrame.linesize, TargetWidth, TargetHeight, OutputFormat, 1) < 0)
		throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_ALLOCATION_FAILED,
			"Could not allocate frame with new resolution.");
}

void FFMS_VideoSource::ResetOutputFormat() {
	if (SWS) {
		sws_freeContext(SWS);
		SWS = nullptr;
	}

	TargetWidth = -1;
	TargetHeight = -1;
	TargetPixelFormats.clear();

	OutputFormat = FFMS_PIX_FMT(NONE);
	OutputColorSpace = AVCOL_SPC_UNSPECIFIED;
	OutputColorRange = AVCOL_RANGE_UNSPECIFIED;

	OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::ResetInputFormat() {
	InputFormatOverridden = false;
	InputFormat = FFMS_PIX_FMT(NONE);
	InputColorSpace = AVCOL_SPC_UNSPECIFIED;
	InputColorRange = AVCOL_RANGE_UNSPECIFIED;

	ReAdjustOutputFormat();
	OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetVideoProperties() {
	VP.RFFDenominator = CodecContext->time_base.num;
	VP.RFFNumerator = CodecContext->time_base.den;
	if (CodecContext->codec_id == FFMS_ID(H264)) {
		if (VP.RFFNumerator & 1)
			VP.RFFDenominator *= 2;
		else
			VP.RFFNumerator /= 2;
	}
	VP.NumFrames = Frames.VisibleFrameCount();
	VP.TopFieldFirst = DecodeFrame->top_field_first;
	VP.ColorSpace = CodecContext->colorspace;
	VP.ColorRange = CodecContext->color_range;
	// these pixfmt's are deprecated but still used
	if (CodecContext->pix_fmt == FFMS_PIX_FMT(YUVJ420P) ||
		CodecContext->pix_fmt == FFMS_PIX_FMT(YUVJ422P) ||
		CodecContext->pix_fmt == FFMS_PIX_FMT(YUVJ444P)
		)
		VP.ColorRange = AVCOL_RANGE_JPEG;


	VP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
	VP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;

	if (CodecContext->width <= 0 || CodecContext->height <= 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Codec returned zero size video");

	// attempt to correct framerate to the proper NTSC fraction, if applicable
	CorrectRationalFramerate(&VP.FPSNumerator, &VP.FPSDenominator);
	// correct the timebase, if necessary
	CorrectTimebase(&VP, &Frames.TB);

	// Set AR variables
	VP.SARNum = CodecContext->sample_aspect_ratio.num;
	VP.SARDen = CodecContext->sample_aspect_ratio.den;

	// Set input and output formats now that we have a CodecContext
	DetectInputFormat();

	OutputFormat = InputFormat;
	OutputColorSpace = InputColorSpace;
	OutputColorRange = InputColorRange;
}

bool FFMS_VideoSource::DecodePacket(AVPacket *Packet) {
	int FrameFinished = 0;
	std::swap(DecodeFrame, LastDecodedFrame);
	avcodec_decode_video2(CodecContext, DecodeFrame, &FrameFinished, Packet);
	if (!FrameFinished)
		std::swap(DecodeFrame, LastDecodedFrame);

	if (!FrameFinished)
		DelayCounter++;

	if (FrameFinished && InitialDecode == 1)
		InitialDecode = -1;

	// The second half of this is to handle the fact that FrameFinished is not
	// an entirely accurate name. In some cases, a frame can be fully decoded,
	// but still not have any picture data. Some examples of things which cause
	// this are xvid NVOPs and (at the time of writing), ffmpeg's h264 b-frame
	// reordering logic (but seemingly not libav's). The API doesn't distinguish
	// between "no picture data because it needs more packets" and "no picture
	// data because the frame was dropped", so we try to calculate the maximum
	// number of packets we should need to feed into the decoder to get frames,
	// and assume we're in the latter case if we go over that number.
	//
	// I suspect this logic actually returns the wrong end of the dropped
	// sequence in some cases, but it probably doesn't matter with the sort of
	// situations where it's actually used.
	return FrameFinished || (DelayCounter > FFMS_CALCULATE_DELAY && !InitialDecode);
}

void FFMS_VideoSource::FlushFinalFrames() {
	AVPacket Packet;
	InitNullPacket(Packet);
	DecodePacket(&Packet);
}

bool FFMS_VideoSource::HasPendingDelayedFrames() {
	if (InitialDecode == -1) {
		if (DelayCounter > FFMS_CALCULATE_DELAY) {
			--DelayCounter;
			return true;
		}
		InitialDecode = 0;
	}
	return false;
}
