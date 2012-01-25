//  Copyright (c) 2007-2011 The FFmpegSource Project
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

/* if you have this, we'll assume you have a new enough libavutil too */
#if LIBSWSCALE_VERSION_INT >= AV_VERSION_INT(0, 12, 0)
extern "C" {
#include <libavutil/opt.h>
}
#endif



// hack
extern int CPUFeatures;



/***************************
**
** Functions related to initializing swscale and libpostproc contexts, etc.
**
***************************/

int64_t GetSWSCPUFlags() {
	int64_t Flags = 0;

#ifdef SWS_CPU_CAPS_MMX
	if (CPUFeatures & FFMS_CPU_CAPS_MMX)
		Flags |= SWS_CPU_CAPS_MMX;
	if (CPUFeatures & FFMS_CPU_CAPS_MMX2)
		Flags |= SWS_CPU_CAPS_MMX2;
	if (CPUFeatures & FFMS_CPU_CAPS_3DNOW)
		Flags |= SWS_CPU_CAPS_3DNOW;
	if (CPUFeatures & FFMS_CPU_CAPS_ALTIVEC)
		Flags |= SWS_CPU_CAPS_ALTIVEC;
	if (CPUFeatures & FFMS_CPU_CAPS_BFIN)
		Flags |= SWS_CPU_CAPS_BFIN;
#ifdef SWS_CPU_CAPS_SSE2
	if (CPUFeatures & FFMS_CPU_CAPS_SSE2)
		Flags |= SWS_CPU_CAPS_SSE2;
#endif /* SWS_CPU_CAPS_SSE2 */
#endif /* SWS_CPU_CAPS_MMX */

	return Flags;
}

SwsContext *GetSwsContext(int SrcW, int SrcH, PixelFormat SrcFormat, int DstW, int DstH, PixelFormat DstFormat, int64_t Flags, int ColorSpace, int ColorRange) {
	Flags |= SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP;
#if LIBSWSCALE_VERSION_INT < AV_VERSION_INT(0, 12, 0)
	return sws_getContext(SrcW, SrcH, SrcFormat, DstW, DstH, DstFormat, Flags, 0, 0, 0);
#else
	SwsContext *Context = sws_alloc_context();
	if (!Context) return 0;

	// The intention here is to never change the color range.
	int Range; // 0 = limited range, 1 = full range
	if (ColorRange == AVCOL_RANGE_JPEG)
		Range = 1;
	else // explicit limited range, or unspecified
		Range = 0;

	av_set_int(Context, "sws_flags", Flags);
	av_set_int(Context, "srcw",       SrcW);
	av_set_int(Context, "srch",       SrcH);
	av_set_int(Context, "dstw",       DstW);
	av_set_int(Context, "dsth",       DstH);
	av_set_int(Context, "src_range",  Range);
	av_set_int(Context, "dst_range",  Range);
	av_set_int(Context, "src_format", SrcFormat);
	av_set_int(Context, "dst_format", DstFormat);

	sws_setColorspaceDetails(Context, sws_getCoefficients(ColorSpace), Range, sws_getCoefficients(ColorSpace), Range, 0, 1<<16, 1<<16);

	if(sws_init_context(Context, 0, 0) < 0){
		sws_freeContext(Context);
		return 0;
	}

	return Context;
#endif

}

int GetPPCPUFlags() {
	int Flags = 0;

#ifdef FFMS_USE_POSTPROC
// not exactly a pretty solution but it'll never get called anyway
	if (CPUFeatures & FFMS_CPU_CAPS_MMX)
		Flags |= PP_CPU_CAPS_MMX;
	if (CPUFeatures & FFMS_CPU_CAPS_MMX2)
		Flags |= PP_CPU_CAPS_MMX2;
	if (CPUFeatures & FFMS_CPU_CAPS_3DNOW)
		Flags |= PP_CPU_CAPS_3DNOW;
	if (CPUFeatures & FFMS_CPU_CAPS_ALTIVEC)
		Flags |= PP_CPU_CAPS_ALTIVEC;
#endif // FFMS_USE_POSTPROC

	return Flags;
}


int GetSwsAssumedColorSpace(int W, int H) {
	if (W > 1024 || H >= 600)
		return SWS_CS_ITU709;
	else
		return SWS_CS_DEFAULT;
}



/***************************
**
** Two functions for making FFMS pretend it's not quite as VFR-based as it really is.
**
***************************/


// attempt to correct framerate to the proper NTSC fraction, if applicable
// code stolen from Perian
void CorrectNTSCRationalFramerate(int *Num, int *Den) {
	AVRational TempFPS;
	TempFPS.den = *Num; // not a typo
	TempFPS.num = *Den; // still not a typo

	av_reduce(&TempFPS.num, &TempFPS.den, TempFPS.num, TempFPS.den, INT_MAX);

	if (TempFPS.num == 1) {
		*Num = TempFPS.den;
		*Den = TempFPS.num;
	}
	else {
		double FTimebase = av_q2d(TempFPS);
		double NearestNTSC = floor(FTimebase * 1001.0 + 0.5) / 1001.0;
		const double SmallInterval = 1.0/120.0;

		if (fabs(FTimebase - NearestNTSC) < SmallInterval) {
			*Num = int((1001.0 / FTimebase) + 0.5);
			*Den = 1001;
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

static BCSType GuessCSType(PixelFormat p) {
	// guessing the colorspace type from the name is kinda hackish but libav doesn't export this kind of metadata
	if (av_pix_fmt_descriptors[p].flags & PIX_FMT_HWACCEL)
		return cUNUSABLE;
	const char *n = av_get_pix_fmt_name(p);
	if (strstr(n, "gray") || strstr(n, "mono") || strstr(n, "y400a"))
		return cGRAY;
	if (strstr(n, "rgb") || strstr(n, "bgr") || strstr(n, "pal8"))
		return cRGB;
	if (strstr(n, "yuv") || strstr(n, "yv") || strstr(n, "nv12") || strstr(n, "nv21"))
		return cYUV;
	return cUNUSABLE; // should never come here
}

struct LossAttributes {
	PixelFormat Format;
	int ChromaUndersampling;
	int ChromaOversampling;
	int DepthDifference;
	int CSLoss; // 0 = same, 1 = no real loss (gray => yuv/rgb), 2 = full conversion required, 3 = complete color loss
};

static int GetPseudoDepth(const AVPixFmtDescriptor &Desc) {
	// Comparing the pseudo depth makes sure that rgb565-ish formats get selected over rgb555-ish ones
	int depth = -1;
	for (int i = 0; i < Desc.nb_components; i++)
		depth = FFMAX(depth, Desc.comp[i].depth_minus1);
	return depth + 1;
}

static LossAttributes CalculateLoss(PixelFormat Dst, PixelFormat Src) {
	const AVPixFmtDescriptor &SrcDesc = av_pix_fmt_descriptors[Src];
	const AVPixFmtDescriptor &DstDesc = av_pix_fmt_descriptors[Dst];
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
		Loss.CSLoss = 3;
	} else { // conversions between RGB and YUV here
		Loss.CSLoss = 2;
	}

	return Loss;
}

PixelFormat FindBestPixelFormat(const std::vector<PixelFormat> &Dsts, PixelFormat Src) {
	// some trivial special cases to make sure there's as little conversion as possible
	if (Dsts.empty())
		return PIX_FMT_NONE;
	if (Dsts.size() == 1)
		return Dsts[0];

	// is the input in the output?
	std::vector<PixelFormat>::const_iterator i = std::find(Dsts.begin(), Dsts.end(), Src);
	if (i != Dsts.end())
		return Src;

	i = Dsts.begin();
	LossAttributes Loss = CalculateLoss(*i++, Src);
	for (; i != Dsts.end(); ++i) {
		LossAttributes CLoss = CalculateLoss(*i, Src);
		if (Loss.CSLoss == 3 && CLoss.CSLoss < 3) { // Preserve chroma information at any cost
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
