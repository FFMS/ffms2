//  Copyright (c) 2007-2015 The FFmpegSource Project
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


#include "videoutils.h"

#include <algorithm>
#include <cmath>

/* if you have this, we'll assume you have a new enough libavutil too */
extern "C" {
#include <libavutil/opt.h>
}

SwsContext *GetSwsContext(int SrcW, int SrcH, AVPixelFormat SrcFormat, int SrcColorSpace, int SrcColorRange, int DstW, int DstH, AVPixelFormat DstFormat, int DstColorSpace, int DstColorRange, int64_t Flags) {
	Flags |= SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND;
	SwsContext *Context = sws_alloc_context();
	if (!Context) return nullptr;

	// 0 = limited range, 1 = full range
	int SrcRange = SrcColorRange == AVCOL_RANGE_JPEG;
	int DstRange = DstColorRange == AVCOL_RANGE_JPEG;

	av_opt_set_int(Context, "sws_flags",  Flags, 0);
	av_opt_set_int(Context, "srcw",       SrcW, 0);
	av_opt_set_int(Context, "srch",       SrcH, 0);
	av_opt_set_int(Context, "dstw",       DstW, 0);
	av_opt_set_int(Context, "dsth",       DstH, 0);
	av_opt_set_int(Context, "src_range",  SrcRange, 0);
	av_opt_set_int(Context, "dst_range",  DstRange, 0);
	av_opt_set_int(Context, "src_format", SrcFormat, 0);
	av_opt_set_int(Context, "dst_format", DstFormat, 0);

	sws_setColorspaceDetails(Context,
		sws_getCoefficients(SrcColorSpace), SrcRange,
		sws_getCoefficients(DstColorSpace), DstRange,
		0, 1<<16, 1<<16);

	if(sws_init_context(Context, nullptr, nullptr) < 0){
		sws_freeContext(Context);
		return nullptr;
	}

	return Context;
}

/***************************
**
** Two functions for making FFMS pretend it's not quite as VFR-based as it really is.
**
***************************/


// attempt to correct framerate to a common fraction if close to one
void CorrectRationalFramerate(int *Num, int *Den) {
	// Make sure fps is a normalized rational number
	av_reduce(Den, Num, *Den, *Num, INT_MAX);

	const double fps = static_cast<double>(*Num) / *Den;
	const int fpsList[] = { 24, 25, 30, 48, 50, 60, 100, 120 };

	for (size_t i = 0; i < sizeof(fpsList) / sizeof(fpsList[0]); i++) {
		const double delta = (fpsList[i] - static_cast<double>(fpsList[i]) / 1.001) / 2.0;
		if (fabs(fps - fpsList[i]) < delta) {
			*Num = fpsList[i];
			*Den = 1;
			break;
		} else if ((fpsList[i] % 25) && (fabs(fps - static_cast<double>(fpsList[i]) / 1.001) < delta)) {
			*Num = fpsList[i] * 1000;
			*Den = 1001;
			break;
		}
	}
}

// correct the timebase if it is invalid
void CorrectTimebase(FFMS_VideoProperties *VP, FFMS_TrackTimeBase *TTimebase) {
	double Timebase = (double)TTimebase->Num / TTimebase->Den;
	double FPS = (double)VP->FPSNumerator / VP->FPSDenominator;
	if ((1000/Timebase) / FPS < 1) {
		TTimebase->Den = VP->FPSNumerator;
		TTimebase->Num = (int64_t)VP->FPSDenominator * 1000;
	}
}

/***************************
**
** Since avcodec_find_best_pix_fmt() is broken, we have our own implementation of it here.
**
***************************/

enum BCSType {
	cGRAY,
	cYUV,
	cRGB,
	cUNUSABLE
};

static BCSType GuessCSType(AVPixelFormat p) {
	// guessing the colorspace type from the name is kinda hackish but libav doesn't export this kind of metadata
	if (av_pix_fmt_desc_get(p)->flags & AV_PIX_FMT_FLAG_HWACCEL)
		return cUNUSABLE;
	const char *n = av_get_pix_fmt_name(p);
	if (strstr(n, "gray") || strstr(n, "mono") || strstr(n, "y400a"))
		return cGRAY;
	if (strstr(n, "rgb") || strstr(n, "bgr") || strstr(n, "gbr") || strstr(n, "pal8"))
		return cRGB;
	if (strstr(n, "yuv") || strstr(n, "yv") || strstr(n, "nv12") || strstr(n, "nv21"))
		return cYUV;
	return cUNUSABLE; // should never come here
}

struct LossAttributes {
	AVPixelFormat Format;
	int ChromaUndersampling;
	int ChromaOversampling;
	int DepthDifference;
	int CSLoss; // 0 = no difference, 1 = no real loss (gray => yuv/rgb or alpha plane added), 2 = full conversion required, 3 = alpha loss, 4 = full conversion plus alpha loss, 5 = complete color loss
};

static int GetPseudoDepth(const AVPixFmtDescriptor &Desc) {
	// Comparing the pseudo depth makes sure that rgb565-ish formats get selected over rgb555-ish ones
	int depth = 0;
	for (int i = 0; i < Desc.nb_components; i++)
		depth = FFMAX(depth, Desc.comp[i].depth);
	return depth;
}

static LossAttributes CalculateLoss(AVPixelFormat Dst, AVPixelFormat Src) {
	const AVPixFmtDescriptor &SrcDesc = *av_pix_fmt_desc_get(Src);
	const AVPixFmtDescriptor &DstDesc = *av_pix_fmt_desc_get(Dst);
	BCSType SrcCS = GuessCSType(Src);
	BCSType DstCS = GuessCSType(Dst);

	LossAttributes Loss;
	Loss.Format = Dst;
	Loss.DepthDifference = GetPseudoDepth(DstDesc) - GetPseudoDepth(SrcDesc);;
	Loss.ChromaOversampling = FFMAX(0, SrcDesc.log2_chroma_h - DstDesc.log2_chroma_h) + FFMAX(0, SrcDesc.log2_chroma_w - DstDesc.log2_chroma_w);
	Loss.ChromaUndersampling = FFMAX(0, DstDesc.log2_chroma_h - SrcDesc.log2_chroma_h) + FFMAX(0, DstDesc.log2_chroma_w - SrcDesc.log2_chroma_w);

	if (SrcCS == DstCS) {
		Loss.CSLoss = 0;
	} else if (SrcCS == cGRAY) {
		Loss.ChromaOversampling = 10; // 10 is kinda arbitrarily chosen here, mostly to make it bigger than over/undersampling value that actually could appear
		Loss.ChromaUndersampling = 0;
		Loss.CSLoss = 0; // maybe set it to 1 as a special case but the chroma oversampling should have a similar effect
	} else if (DstCS == cGRAY) {
		Loss.ChromaOversampling = 0;
		Loss.ChromaUndersampling = 10;
		Loss.CSLoss = 5;
	} else { // conversions between RGB and YUV here
		Loss.CSLoss = 2;
	}

	if (Loss.CSLoss < 3 && (DstDesc.nb_components == SrcDesc.nb_components - 1)) {
		if (Loss.CSLoss == 2)
			Loss.CSLoss = 4;
		else
			Loss.CSLoss = 3;
	}

	if (Loss.CSLoss == 0 && (SrcDesc.nb_components == DstDesc.nb_components - 1)) {
		Loss.CSLoss = 1;
	}

	return Loss;
}

AVPixelFormat FindBestPixelFormat(const std::vector<AVPixelFormat> &Dsts, AVPixelFormat Src) {
	// some trivial special cases to make sure there's as little conversion as possible
	if (Dsts.empty())
		return AV_PIX_FMT_NONE;
	if (Dsts.size() == 1)
		return Dsts[0];

	// is the input in the output?
	auto i = std::find(Dsts.begin(), Dsts.end(), Src);
	if (i != Dsts.end())
		return Src;

	// If it's an evil paletted format pretend it's normal RGB when calculating loss
    if (Src == AV_PIX_FMT_PAL8)
		Src = AV_PIX_FMT_RGB32;

	i = Dsts.begin();
	LossAttributes Loss = CalculateLoss(*i++, Src);
	for (; i != Dsts.end(); ++i) {
		LossAttributes CLoss = CalculateLoss(*i, Src);
		if (Loss.CSLoss >= 3 && CLoss.CSLoss < Loss.CSLoss) { // favor the same color format output
			Loss = CLoss;
		} else if (Loss.DepthDifference >= 0 && CLoss.DepthDifference >= 0) { // focus on chroma undersamling and conversion loss if the target depth has been achieved
			if ((CLoss.ChromaUndersampling < Loss.ChromaUndersampling)
				|| (CLoss.ChromaUndersampling == Loss.ChromaUndersampling && CLoss.CSLoss < Loss.CSLoss)
				|| (CLoss.ChromaUndersampling == Loss.ChromaUndersampling && CLoss.CSLoss == Loss.CSLoss && CLoss.DepthDifference < Loss.DepthDifference)
				|| (CLoss.ChromaUndersampling == Loss.ChromaUndersampling && CLoss.CSLoss == Loss.CSLoss
					&& CLoss.DepthDifference == Loss.DepthDifference && CLoss.ChromaOversampling < Loss.ChromaOversampling))
				Loss = CLoss;
		} else { // put priority on reaching the same depth as the input
			if ((CLoss.DepthDifference > Loss.DepthDifference)
				|| (CLoss.DepthDifference == Loss.DepthDifference && CLoss.ChromaUndersampling < Loss.ChromaUndersampling)
				|| (CLoss.DepthDifference == Loss.DepthDifference && CLoss.ChromaUndersampling == Loss.ChromaUndersampling && CLoss.CSLoss < Loss.CSLoss)
				|| (CLoss.DepthDifference == Loss.DepthDifference && CLoss.ChromaUndersampling == Loss.ChromaUndersampling
					&& CLoss.CSLoss == Loss.CSLoss && CLoss.ChromaOversampling < Loss.ChromaOversampling))
				Loss = CLoss;
		}
	}

	return Loss.Format;
}

namespace {
int parse_vp8(AVCodecParserContext *s,
	AVCodecContext *,
	const uint8_t **poutbuf, int *poutbuf_size,
	const uint8_t *buf, int buf_size)
{
	s->pict_type = (buf[0] & 0x01) ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
	s->repeat_pict = (buf[0] & 0x10) ? 0 : -1;

	*poutbuf = buf;
	*poutbuf_size = buf_size;
	return buf_size;
}

AVCodecParser ffms_vp8_parser = { { AV_CODEC_ID_VP8 }, 0, nullptr, parse_vp8, nullptr, nullptr, nullptr };
}

void RegisterCustomParsers() {
	av_register_codec_parser(&ffms_vp8_parser);
}
