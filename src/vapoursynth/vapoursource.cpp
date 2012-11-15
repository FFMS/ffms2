//  Copyright (c) 2012 Fredrik Mellbin
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

#include "vapoursource.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#include <libavutil/common.h>
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

typedef std::pair<int, int> IntPair;

void BitBlt(uint8_t* dstp, int dst_pitch, const uint8_t* srcp, int src_pitch, int row_size, int height) {
    if (src_pitch == dst_pitch && dst_pitch == row_size) {
        memcpy(dstp, srcp, row_size * height);
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dstp, srcp, row_size);
            dstp += dst_pitch;
            srcp += src_pitch;
        }
    }
}

static int GetNumPixFmts() {
	int n = 0;
	while (av_get_pix_fmt_name((PixelFormat)n))
		n++;
	return n;
}

static bool IsRealPlanar(const AVPixFmtDescriptor &desc) {
	int used_planes = 0;
	for (int i = 0; i < desc.nb_components; i++)
		used_planes = std::max(used_planes, (int)desc.comp[i].plane + 1);
	return (used_planes == desc.nb_components) && (desc.nb_components == 1 || desc.nb_components == 3) && (desc.comp[0].depth_minus1 + 1 >= 8);
}

static int GetColorFamily(const AVPixFmtDescriptor &desc) {
	if (desc.nb_components == 1)
		return cmGray;
	else if (desc.flags & PIX_FMT_RGB)
		return cmRGB;
	else
		return cmYUV;
}

static int formatConversion(int id, bool toPixelFormat, VSCore *core, const VSAPI *vsapi) {
	if (toPixelFormat) {
		const VSFormat *f = vsapi->getFormatPreset(id, core);
		int npixfmt = GetNumPixFmts();
		for (int i = 0; i < npixfmt; i++) {
			const AVPixFmtDescriptor &desc = av_pix_fmt_descriptors[i];
			if (IsRealPlanar(desc)
				&& GetColorFamily(desc) == f->colorFamily
				&& desc.comp[0].depth_minus1 + 1 == f->bitsPerSample
				&& desc.log2_chroma_w == f->subSamplingW
				&& desc.log2_chroma_h == f->subSamplingH)
				return i;
		}
		return PIX_FMT_NONE;
	} else {
		int colorfamily = cmYUV;
		if (av_pix_fmt_descriptors[id].nb_components == 1)
			colorfamily = cmGray;
		else if (av_pix_fmt_descriptors[id].nb_components == 1)
			colorfamily = cmRGB;
		return vsapi->registerFormat(colorfamily, stInteger,
			av_pix_fmt_descriptors[id].comp[0].depth_minus1 + 1,
			av_pix_fmt_descriptors[id].log2_chroma_w,
			av_pix_fmt_descriptors[id].log2_chroma_h, core)->id;
	}
}

void VS_CC VSVideoSource::Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)*instanceData;
	vsapi->setVideoInfo(&vs->VI, 1, node);
}

const VSFrameRef *VS_CC VSVideoSource::GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)*instanceData;
	if (activationReason == arInitial) {
		if (vs->VI.numFrames && n >= vs->VI.numFrames)
			n = vs->VI.numFrames - 1;

		char ErrorMsg[1024];
		FFMS_ErrorInfo E;
		E.Buffer = ErrorMsg;
		E.BufferSize = sizeof(ErrorMsg);
		std::string buf = "Source: ";

		VSFrameRef *Dst = vsapi->newVideoFrame(vs->VI.format, vs->VI.width, vs->VI.height, NULL, core);
		VSMap *Props = vsapi->getFramePropsRW(Dst);

		const FFMS_Frame *Frame;

		if (vs->FPSNum > 0 && vs->FPSDen > 0) {
			Frame = FFMS_GetFrameByTime(vs->V, FFMS_GetVideoProperties(vs->V)->FirstTime +
				(double)(n * (int64_t)vs->FPSDen) / vs->FPSNum, &E);
			vsapi->propSetInt(Props, "_DurationNum", vs->FPSDen, paReplace);
			vsapi->propSetInt(Props, "_DurationDen", vs->FPSNum, paReplace);
		} else {
			Frame = FFMS_GetFrame(vs->V, n, &E);
			FFMS_Track *T = FFMS_GetTrackFromVideo(vs->V);
			const FFMS_TrackTimeBase *TB = FFMS_GetTimeBase(T);
			int64_t num;
			if (n + 1 < vs->VI.numFrames)
				num = FFMS_GetFrameInfo(T, n + 1)->PTS - FFMS_GetFrameInfo(T, n)->PTS;
			else // simply use the second to last frame's duration for the last one, should be good enough
				num = FFMS_GetFrameInfo(T, n)->PTS - FFMS_GetFrameInfo(T, n - 1)->PTS;
			vsapi->propSetInt(Props, "_DurationNum", TB->Num * num, paReplace);
			vsapi->propSetInt(Props, "_DurationDen", TB->Den, paReplace);
		}

		if (Frame == NULL) {
			buf += E.Buffer;
			vsapi->setFilterError(buf.c_str(), frameCtx);
			return NULL;
		}

		// Set AR variables
		if (vs->SARNum > 0 && vs->SARDen > 0) {
			vsapi->propSetInt(Props, "_SARNum", vs->SARNum, paReplace);
			vsapi->propSetInt(Props, "_SARDen", vs->SARDen, paReplace);
		}

		vsapi->propSetInt(Props, "_ColorSpace", Frame->ColorSpace, paReplace);
        if (Frame->ColorRange == 1)
            vsapi->propSetInt(Props, "_ColorRange", 1, paReplace);
        else if (Frame->ColorRange == 2)
            vsapi->propSetInt(Props, "_ColorRange", 0, paReplace);
		vsapi->propSetData(Props, "_PictType", &Frame->PictType, 1, paReplace);

		OutputFrame(Frame, Dst, vsapi, core);

		return Dst;
	}

	return NULL;
}

void VS_CC VSVideoSource::Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)instanceData;
	delete vs;
}

VSVideoSource::VSVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, int Threads, int SeekMode, int RFFMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
		int Format, const VSAPI *vsapi, VSCore *core)
		: FPSNum(FPSNum), FPSDen(FPSDen) {

	memset(&VI, 0, sizeof(VI));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	V = FFMS_CreateVideoSource(SourceFile, Track, Index, Threads, SeekMode, &E);
	if (!V) {
		std::string buf = "Source: ";
		buf += E.Buffer;
		throw std::runtime_error(buf);
	}
	try {
		InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, Format, vsapi, core);
	} catch (std::exception &) {
		FFMS_DestroyVideoSource(V);
		throw;
	}

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

	if (FPSNum > 0 && FPSDen > 0) {
		VI.fpsDen = FPSDen;
		VI.fpsNum = FPSNum;
		if (VP->NumFrames > 1) {
			VI.numFrames = static_cast<int>((VP->LastTime - VP->FirstTime) * (1 + 1. / (VP->NumFrames - 1)) * FPSNum / FPSDen + 0.5);
			if (VI.numFrames < 1) VI.numFrames = 1;
		} else {
			VI.numFrames = 1;
		}
	} else {
		VI.fpsDen = VP->FPSDenominator;
		VI.fpsNum = VP->FPSNumerator;
		VI.numFrames = VP->NumFrames;
	}

	SARNum = VP->SARNum;
	SARDen = VP->SARDen;
}

VSVideoSource::~VSVideoSource() {
	FFMS_DestroyVideoSource(V);
}

void VSVideoSource::InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
		const char *ResizerName, int ConvertToFormat, const VSAPI *vsapi, VSCore *core) {

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
	const FFMS_Frame *F = FFMS_GetFrame(V, 0, &E);
	if (!F) {
		std::string buf = "Source: ";
		buf += E.Buffer;
		throw std::runtime_error(buf);
	}

	std::vector<int> TargetFormats;
	int npixfmt = GetNumPixFmts();
	for (int i = 0; i < npixfmt; i++)
		if (IsRealPlanar(av_pix_fmt_descriptors[i]))
			TargetFormats.push_back(i);
	TargetFormats.push_back(PIX_FMT_NONE);

	int TargetPixelFormat = PIX_FMT_NONE;
	if (ConvertToFormat != pfNone) {
		TargetPixelFormat = formatConversion(ConvertToFormat, true, core, vsapi);
		if (TargetPixelFormat == PIX_FMT_NONE)
			throw std::runtime_error(std::string("Source: Invalid output colorspace specified"));

		TargetFormats.clear();
		TargetFormats.push_back(TargetPixelFormat);
		TargetFormats.push_back(-1);
	}

	if (ResizeToWidth <= 0)
		ResizeToWidth = F->EncodedWidth;

	if (ResizeToHeight <= 0)
		ResizeToHeight = F->EncodedHeight;

	/* fixme, ignore the resizer for now since nobody ever uses it
	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		throw std::runtime_error(std::string("Source: Invalid resizer name specified"));
	*/

	int Resizer = SWS_BICUBIC;

	if (FFMS_SetOutputFormatV2(V, &TargetFormats[0],
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		throw std::runtime_error(std::string("Source: No suitable output format found"));

	F = FFMS_GetFrame(V, 0, &E);
	TargetFormats.clear();
	TargetFormats.push_back(F->ConvertedPixelFormat);
	TargetFormats.push_back(-1);

	// This trick is required to first get the "best" default format and then set only that format as the output
	if (FFMS_SetOutputFormatV2(V, &TargetFormats[0],
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		throw std::runtime_error(std::string("Source: No suitable output format found"));

	F = FFMS_GetFrame(V, 0, &E);

	VI.format = vsapi->getFormatPreset(formatConversion(F->ConvertedPixelFormat, false, core, vsapi), core);
	if (!VI.format)
		throw std::runtime_error(std::string("Source: No suitable output format found"));

	VI.width = F->ScaledWidth;
	VI.height = F->ScaledHeight;

	// fixme? Crop to obey sane even width/height requirements
}

void VSVideoSource::OutputFrame(const FFMS_Frame *Frame, VSFrameRef *Dst, const VSAPI *vsapi, VSCore *core) {
	const VSFormat *fi = vsapi->getFrameFormat(Dst);
    for (int i = 0; i < fi->numPlanes; i++)
        BitBlt(vsapi->getWritePtr(Dst, i), vsapi->getStride(Dst, i), Frame->Data[i], Frame->Linesize[i],
            vsapi->getFrameWidth(Dst, i) * fi->bytesPerSample, vsapi->getFrameHeight(Dst, i));
}
