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
#include "numthreads.h"

void FFMS_VideoSource::GetFrameCheck(int n) {
	if (n < 0 || n >= VP.NumFrames)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds frame requested");
}

void FFMS_VideoSource::SetPP(const char *PP) {

#ifdef FFMS_USE_POSTPROC
	if (PPMode)
		pp_free_mode(PPMode);
	PPMode = NULL;

	if (PP != NULL && strcmp(PP, "")) {
		// due to a parsing bug in libpostproc it can read beyond the end of a string
		// adding a ',' prevents the bug from manifesting
		// libav head 2011-08-26
		std::string s = PP;
		s.append(",");
		PPMode = pp_get_mode_by_name_and_quality(s.c_str(), PP_QUALITY_MAX);
		if (!PPMode) {
			ResetPP();
			throw FFMS_Exception(FFMS_ERROR_POSTPROCESSING, FFMS_ERROR_INVALID_ARGUMENT,
				"Invalid postprocesing settings");
		}

	}

	ReAdjustPP(CodecContext->pix_fmt, CodecContext->width, CodecContext->height);
	OutputFrame(DecodeFrame);
#else
	throw FFMS_Exception(FFMS_ERROR_POSTPROCESSING, FFMS_ERROR_UNSUPPORTED,
		"FFMS2 was not compiled with postprocessing support");
#endif /* FFMS_USE_POSTPROC */
}

void FFMS_VideoSource::ResetPP() {
#ifdef FFMS_USE_POSTPROC
	if (PPContext)
		pp_free_context(PPContext);
	PPContext = NULL;

	if (PPMode)
		pp_free_mode(PPMode);
	PPMode = NULL;

#endif /* FFMS_USE_POSTPROC */
	OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::ReAdjustPP(PixelFormat VPixelFormat, int Width, int Height) {
#ifdef FFMS_USE_POSTPROC
	if (PPContext)
		pp_free_context(PPContext);
	PPContext = NULL;

	if (!PPMode)
		return;

	int Flags =  GetPPCPUFlags();

	switch (VPixelFormat) {
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			Flags |= PP_FORMAT_420; break;
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
			Flags |= PP_FORMAT_422; break;
		case PIX_FMT_YUV411P:
			Flags |= PP_FORMAT_411; break;
		case PIX_FMT_YUV444P:
		case PIX_FMT_YUVJ444P:
			Flags |= PP_FORMAT_444; break;
		default:
			ResetPP();
			throw FFMS_Exception(FFMS_ERROR_POSTPROCESSING, FFMS_ERROR_UNSUPPORTED,
				"The video does not have a colorspace suitable for postprocessing");
	}

	PPContext = pp_get_context(Width, Height, Flags);

	avpicture_free(&PPFrame);
	avpicture_alloc(&PPFrame, VPixelFormat, Width, Height);
#else
	return;
#endif /* FFMS_USE_POSTPROC */

}


static void CopyAVPictureFields(AVPicture &Picture, FFMS_Frame &Dst) {
	for (int i = 0; i < 4; i++) {
		Dst.Data[i] = Picture.data[i];
		Dst.Linesize[i] = Picture.linesize[i];
	}
}


// this might look stupid, but we have actually had crashes caused by not checking like this.
static void SanityCheckFrameForData(AVFrame *Frame) {
	for (int i = 0; i < 4; i++) {
		if (Frame->data[i] != NULL && Frame->linesize[i] > 0)
			return;
	}

	throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC, "Insanity detected: decoder returned an empty frame");
}


FFMS_Frame *FFMS_VideoSource::OutputFrame(AVFrame *Frame) {
	SanityCheckFrameForData(Frame);

	if (LastFrameWidth != CodecContext->width || LastFrameHeight != CodecContext->height || LastFramePixelFormat != CodecContext->pix_fmt) {
		ReAdjustPP(CodecContext->pix_fmt, CodecContext->width, CodecContext->height);

		if (TargetHeight > 0 && TargetWidth > 0 && !TargetPixelFormats.empty())
			ReAdjustOutputFormat();
	}

#ifdef FFMS_USE_POSTPROC
	if (PPMode) {
		pp_postprocess(const_cast<const uint8_t **>(Frame->data), Frame->linesize, PPFrame.data, PPFrame.linesize, CodecContext->width, CodecContext->height, Frame->qscale_table, Frame->qstride, PPMode, PPContext, Frame->pict_type | (Frame->qscale_type ? PP_PICT_TYPE_QP2 : 0));
		if (SWS) {
			sws_scale(SWS, PPFrame.data, PPFrame.linesize, 0, CodecContext->height, SWSFrame.data, SWSFrame.linesize);
			CopyAVPictureFields(SWSFrame, LocalFrame);
		} else {
			CopyAVPictureFields(PPFrame, LocalFrame);
		}
	} else {
		if (SWS) {
			sws_scale(SWS, Frame->data, Frame->linesize, 0, CodecContext->height, SWSFrame.data, SWSFrame.linesize);
			CopyAVPictureFields(SWSFrame, LocalFrame);
		} else {
			// Special case to avoid ugly casts
			for (int i = 0; i < 4; i++) {
				LocalFrame.Data[i] = Frame->data[i];
				LocalFrame.Linesize[i] = Frame->linesize[i];
			}
		}
	}
#else // FFMS_USE_POSTPROC
	if (SWS) {
		sws_scale(SWS, Frame->data, Frame->linesize, 0, CodecContext->height, SWSFrame.data, SWSFrame.linesize);
		CopyAVPictureFields(SWSFrame, LocalFrame);
	} else {
		// Special case to avoid ugly casts
		for (int i = 0; i < 4; i++) {
			LocalFrame.Data[i] = Frame->data[i];
			LocalFrame.Linesize[i] = Frame->linesize[i];
		}
	}
#endif // FFMS_USE_POSTPROC

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

	LastFrameHeight = CodecContext->height;
	LastFrameWidth = CodecContext->width;
	LastFramePixelFormat = CodecContext->pix_fmt;

	return &LocalFrame;
}

FFMS_VideoSource::FFMS_VideoSource(const char *SourceFile, FFMS_Index &Index, int Track, int Threads)
: Index(Index)
, CodecContext(NULL)
{
	if (Track < 0 || Track >= static_cast<int>(Index.size()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds track index selected");

	if (Index[Track].TT != FFMS_TYPE_VIDEO)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Not a video track");

	if (Index[Track].size() == 0)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Video track contains no frames");

	if (!Index.CompareFileSignature(SourceFile))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
			"The index does not match the source file");

	Frames = Index[Track];
	VideoTrack = Track;

	memset(&VP, 0, sizeof(VP));
#ifdef FFMS_USE_POSTPROC
	PPContext = NULL;
	PPMode = NULL;
#endif // FFMS_USE_POSTPROC
	SWS = NULL;
	LastFrameNum = 0;
	CurrentFrame = 1;
	DelayCounter = 0;
	InitialDecode = 1;
	CodecContext = NULL;
	LastFrameHeight = -1;
	LastFrameWidth = -1;
	LastFramePixelFormat = PIX_FMT_NONE;
	TargetHeight = -1;
	TargetWidth = -1;
	TargetResizer = 0;
	OutputFormat = PIX_FMT_NONE;
	OutputColorSpace = AVCOL_SPC_UNSPECIFIED;
	OutputColorRange = AVCOL_RANGE_UNSPECIFIED;
	if (Threads < 1)
		DecodingThreads = GetNumberOfLogicalCPUs();
	else
		DecodingThreads = Threads;
	DecodeFrame = avcodec_alloc_frame();

	// Dummy allocations so the unallocated case doesn't have to be handled later
#ifdef FFMS_USE_POSTPROC
	avpicture_alloc(&PPFrame, PIX_FMT_GRAY8, 16, 16);
#endif // FFMS_USE_POSTPROC
	avpicture_alloc(&SWSFrame, PIX_FMT_GRAY8, 16, 16);

	Index.AddRef();
}

FFMS_VideoSource::~FFMS_VideoSource() {
#ifdef FFMS_USE_POSTPROC
	if (PPMode)
		pp_free_mode(PPMode);

	if (PPContext)
		pp_free_context(PPContext);

	avpicture_free(&PPFrame);
#endif // FFMS_USE_POSTPROC

	if (SWS)
		sws_freeContext(SWS);

	avpicture_free(&SWSFrame);
	av_freep(&DecodeFrame);

	Index.Release();
}

FFMS_Frame *FFMS_VideoSource::GetFrameByTime(double Time) {
	int Frame = Frames.ClosestFrameFromPTS(static_cast<int64_t>((Time * 1000 * Frames.TB.Den) / Frames.TB.Num));
	return GetFrame(Frame);
}

void FFMS_VideoSource::SetOutputFormat(const PixelFormat *TargetFormats, int Width, int Height, int Resizer) {
	TargetWidth = Width;
	TargetHeight = Height;
	TargetResizer = Resizer;
	TargetPixelFormats.clear();
	while (*TargetFormats != PIX_FMT_NONE) {
		TargetPixelFormats.push_back(*TargetFormats);
		TargetFormats++;
	}
	ReAdjustOutputFormat();
	OutputFrame(DecodeFrame);
}

static int handle_jpeg(PixelFormat *format)
{
	switch (*format) {
		case PIX_FMT_YUVJ420P: *format = PIX_FMT_YUV420P; return AVCOL_RANGE_JPEG;
		case PIX_FMT_YUVJ422P: *format = PIX_FMT_YUV422P; return AVCOL_RANGE_JPEG;
		case PIX_FMT_YUVJ444P: *format = PIX_FMT_YUV444P; return AVCOL_RANGE_JPEG;
		case PIX_FMT_YUVJ440P: *format = PIX_FMT_YUV440P; return AVCOL_RANGE_JPEG;
		default:                                          return AVCOL_RANGE_UNSPECIFIED;
	}
}

void FFMS_VideoSource::ReAdjustOutputFormat() {
	if (SWS) {
		sws_freeContext(SWS);
		SWS = NULL;
	}

	OutputFormat = FindBestPixelFormat(TargetPixelFormats, CodecContext->pix_fmt);

	if (OutputFormat == PIX_FMT_NONE) {
		ResetOutputFormat();
		throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
			"No suitable output format found");
	}

	PixelFormat InputFormat = CodecContext->pix_fmt;
	int ColorRange = handle_jpeg(&InputFormat);
	if (ColorRange == AVCOL_RANGE_UNSPECIFIED) {
		if (CodecContext->color_range == AVCOL_RANGE_UNSPECIFIED) {
			OutputColorRange = AVCOL_RANGE_UNSPECIFIED;
			ColorRange = AVCOL_RANGE_MPEG; // assumption
		}
		else {
			OutputColorRange = CodecContext->color_range;
			ColorRange = CodecContext->color_range;
		}
	}

	if (InputFormat != OutputFormat || TargetWidth != CodecContext->width || TargetHeight != CodecContext->height) {
		int ColorSpace = CodecContext->colorspace;
		if (ColorSpace == AVCOL_SPC_UNSPECIFIED) {
			ColorSpace = GetSwsAssumedColorSpace(CodecContext->width, CodecContext->height);
			OutputColorSpace = (AVColorSpace)ColorSpace;
		}

		SWS = GetSwsContext(CodecContext->width, CodecContext->height, CodecContext->pix_fmt, TargetWidth, TargetHeight,
			OutputFormat, GetSWSCPUFlags() | TargetResizer, ColorSpace, ColorRange);
		if (SWS == NULL) {
			ResetOutputFormat();
			throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
				"Failed to allocate SWScale context");
		}
	}

	avpicture_free(&SWSFrame);
	avpicture_alloc(&SWSFrame, OutputFormat, TargetWidth, TargetHeight);
}

void FFMS_VideoSource::ResetOutputFormat() {
	if (SWS) {
		sws_freeContext(SWS);
		SWS = NULL;
	}

	TargetWidth = -1;
	TargetHeight = -1;
	TargetPixelFormats.clear();
	OutputFormat = PIX_FMT_NONE;

	OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetVideoProperties() {
	VP.RFFDenominator = CodecContext->time_base.num;
	VP.RFFNumerator = CodecContext->time_base.den;
	if (CodecContext->codec_id == CODEC_ID_H264) {
		if (VP.RFFNumerator & 1)
			VP.RFFDenominator *= 2;
		else
			VP.RFFNumerator /= 2;
	}
	VP.NumFrames = Frames.size();
	VP.TopFieldFirst = DecodeFrame->top_field_first;
	VP.ColorSpace = CodecContext->colorspace;
	VP.ColorRange = CodecContext->color_range;
	// these pixfmt's are deprecated but still used
	if (
		CodecContext->pix_fmt == PIX_FMT_YUVJ420P
		|| CodecContext->pix_fmt == PIX_FMT_YUVJ422P
		|| CodecContext->pix_fmt == PIX_FMT_YUVJ444P
		)
		VP.ColorRange = AVCOL_RANGE_JPEG;


	VP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
	VP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;

	if (CodecContext->width <= 0 || CodecContext->height <= 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
		"Codec returned zero size video");

	// attempt to correct framerate to the proper NTSC fraction, if applicable
	CorrectNTSCRationalFramerate(&VP.FPSNumerator, &VP.FPSDenominator);
	// correct the timebase, if necessary
	CorrectTimebase(&VP, &Frames.TB);

	// Set AR variables
	VP.SARNum = CodecContext->sample_aspect_ratio.num;
	VP.SARDen = CodecContext->sample_aspect_ratio.den;
}
