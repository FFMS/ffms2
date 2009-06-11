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

#include "ffvideosource.h"

int FFVideo::InitPP(const char *PP, PixelFormat PixelFormat, char *ErrorMsg, unsigned MsgSize) {
	if (PP == NULL || !strcmp(PP, ""))
		return 0;

	PPMode = pp_get_mode_by_name_and_quality(PP, PP_QUALITY_MAX);
	if (!PPMode) {
		snprintf(ErrorMsg, MsgSize, "Invalid postprocesing settings");
		return 1;
	}

	int Flags =  GetCPUFlags();

	switch (PixelFormat) {
		case PIX_FMT_YUV420P: Flags |= PP_FORMAT_420; break;
		case PIX_FMT_YUV422P: Flags |= PP_FORMAT_422; break;
		case PIX_FMT_YUV411P: Flags |= PP_FORMAT_411; break;
		case PIX_FMT_YUV444P: Flags |= PP_FORMAT_444; break;
		default:
			snprintf(ErrorMsg, MsgSize, "Input format is not supported for postprocessing");
			return 2;
	}

	PPContext = pp_get_context(VP.Width, VP.Height, Flags);

	if (!(PPFrame = avcodec_alloc_frame())) {
		snprintf(ErrorMsg, MsgSize, "Failed to allocate temporary frame");
		return 3;
	}

	if (avpicture_alloc((AVPicture *)PPFrame, PixelFormat, VP.Width, VP.Height) < 0) {
		av_free(PPFrame);
		PPFrame = NULL;
		snprintf(ErrorMsg, MsgSize, "Failed to allocate picture");
		return 4;
	}

	FinalFrame = PPFrame;

	return 0;
}

TAVFrameLite *FFVideo::OutputFrame(AVFrame *Frame) {
	if (PPContext) {
		pp_postprocess(const_cast<const uint8_t **>(Frame->data), Frame->linesize, PPFrame->data, PPFrame->linesize, VP.Width, VP.Height, Frame->qscale_table, Frame->qstride, PPMode, PPContext, Frame->pict_type | (Frame->qscale_type ? PP_PICT_TYPE_QP2 : 0));
		PPFrame->key_frame = Frame->key_frame;
		PPFrame->pict_type = Frame->pict_type;
	}

	if (SWS) {
		sws_scale(SWS, PPFrame->data, PPFrame->linesize, 0, VP.Height, FinalFrame->data, FinalFrame->linesize);
		FinalFrame->key_frame = PPFrame->key_frame;
		FinalFrame->pict_type = PPFrame->pict_type;
	}

	return reinterpret_cast<TAVFrameLite *>(FinalFrame);
}

FFVideo::FFVideo() {
	memset(&VP, 0, sizeof(VP));
	PPContext = NULL;
	PPMode = NULL;
	SWS = NULL;
	LastFrameNum = 0;
	CurrentFrame = 1;
	CodecContext = NULL;
	DecodeFrame = avcodec_alloc_frame();
	PPFrame = DecodeFrame;
	FinalFrame = PPFrame;
}

FFVideo::~FFVideo() {
	if (PPMode)
		pp_free_mode(PPMode);
	if (PPContext)
		pp_free_context(PPContext);
	if (SWS)
		sws_freeContext(SWS);
	if (FinalFrame != PPFrame) {
		avpicture_free((AVPicture *)FinalFrame);
		av_free(FinalFrame);
	}
	if (PPFrame != DecodeFrame) {
		avpicture_free((AVPicture *)PPFrame);
		av_free(PPFrame);
	}
	av_free(DecodeFrame);
}

TAVFrameLite *FFVideo::GetFrameByTime(double Time, char *ErrorMsg, unsigned MsgSize) {
	int Frame = Frames.ClosestFrameFromDTS(static_cast<int64_t>((Time * 1000 * Frames.TB.Den) / Frames.TB.Num));
	return GetFrame(Frame, ErrorMsg, MsgSize);
}

int FFVideo::SetOutputFormat(int64_t TargetFormats, int Width, int Height, char *ErrorMsg, unsigned MsgSize) {
	int Loss;
	PixelFormat OutputFormat = avcodec_find_best_pix_fmt(TargetFormats,
		CodecContext->pix_fmt, 1 /* Required to prevent pointless RGB32 => RGB24 conversion */, &Loss);
	if (OutputFormat == PIX_FMT_NONE) {
		snprintf(ErrorMsg, MsgSize, "No suitable output format found");
		return -1;
	}

	SwsContext *NewSWS = NULL;
	if (CodecContext->pix_fmt != OutputFormat || Width != CodecContext->width || Height != CodecContext->height) {
		NewSWS = sws_getContext(CodecContext->width, CodecContext->height, CodecContext->pix_fmt, Width, Height,
			OutputFormat, GetCPUFlags() | SWS_BICUBIC, NULL, NULL, NULL);
		if (NewSWS == NULL) {
			snprintf(ErrorMsg, MsgSize, "Failed to allocate SWScale context");
			return 1;
		}
	}

	if (SWS)
		sws_freeContext(SWS);
	SWS = NewSWS;

	VP.Height = Height;
	VP.Width = Width;
	VP.VPixelFormat = OutputFormat;

	// FIXME: In theory the allocations in this part could fail just like in InitPP but whatever
	if (FinalFrame != PPFrame) {
		avpicture_free((AVPicture *)FinalFrame);
		av_free(FinalFrame);
	}

	if (SWS) {
		FinalFrame = avcodec_alloc_frame();
		avpicture_alloc((AVPicture *)FinalFrame, static_cast<PixelFormat>(VP.VPixelFormat), VP.Width, VP.Height);
	} else {
		FinalFrame = PPFrame;
	}

	return 0;
}

void FFVideo::ResetOutputFormat() {
	if (SWS)
		sws_freeContext(SWS);
	SWS = NULL;
	VP.Height = CodecContext->height;
	VP.Width = CodecContext->width;
	VP.VPixelFormat = CodecContext->pix_fmt;
}
