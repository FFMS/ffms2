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

#include "ffswscale.h"
#include "avsutils.h"
#include "../core/videoutils.h"

static int64_t AvisynthToSWSCPUFlags(long AvisynthFlags) {
	int64_t Flags = 0;
#ifdef SWS_CPU_CAPS_MMX
	if (AvisynthFlags & CPUF_MMX)
		Flags |= SWS_CPU_CAPS_MMX;
	if (AvisynthFlags & CPUF_INTEGER_SSE)
		Flags |= SWS_CPU_CAPS_MMX2;
	if (AvisynthFlags & CPUF_3DNOW_EXT)
		Flags |= SWS_CPU_CAPS_3DNOW;
	if (AvisynthFlags & CPUF_SSE2)
		Flags |= SWS_CPU_CAPS_SSE2;
#endif
	return Flags;
}

SWScale::SWScale(PClip Child, int ResizeToWidth, int ResizeToHeight, const char *ResizerName, const char *ConvertToFormatName, IScriptEnvironment *Env) : GenericVideoFilter(Child) {
	Context = NULL;
	OrigWidth = vi.width;
	OrigHeight = vi.height;
	FlipOutput = vi.IsYUV();

	PixelFormat ConvertFromFormat = PIX_FMT_NONE;
	if (vi.IsYV12())
		ConvertFromFormat = PIX_FMT_YUV420P;
	if (vi.IsYUY2())
		ConvertFromFormat = PIX_FMT_YUYV422;
	if (vi.IsRGB24())
		ConvertFromFormat = PIX_FMT_BGR24;
	if (vi.IsRGB32())
		ConvertFromFormat = PIX_FMT_RGB32;

	if (ResizeToHeight <= 0)
		ResizeToHeight = OrigHeight;
	else
		vi.height = ResizeToHeight;

	if (ResizeToWidth <= 0)
		ResizeToWidth = OrigWidth;
	else
		vi.width = ResizeToWidth;

	PixelFormat ConvertToFormat = CSNameToPIXFMT(ConvertToFormatName, ConvertFromFormat);
	if (ConvertToFormat == PIX_FMT_NONE)
		Env->ThrowError("SWScale: Invalid colorspace specified (%s)", ConvertToFormatName);

	switch (ConvertToFormat) {
		case PIX_FMT_YUV420P: vi.pixel_type = VideoInfo::CS_I420; break;
		case PIX_FMT_YUYV422: vi.pixel_type = VideoInfo::CS_YUY2; break;
		case PIX_FMT_BGR24: vi.pixel_type = VideoInfo::CS_BGR24; break;
		case PIX_FMT_RGB32: vi.pixel_type = VideoInfo::CS_BGR32; break;
	}

	FlipOutput ^= vi.IsYUV();

	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		Env->ThrowError("SWScale: Invalid resizer specified (%s)", ResizerName);

	if (ConvertToFormat == PIX_FMT_YUV420P && vi.height & 1)
		Env->ThrowError("SWScale: mod 2 output height required");

	if ((ConvertToFormat == PIX_FMT_YUV420P || ConvertToFormat == PIX_FMT_YUYV422) && vi.width & 1)
		Env->ThrowError("SWScale: mod 2 output width required");

	Context = GetSwsContext(
		OrigWidth, OrigHeight, ConvertFromFormat, GetAssumedColorSpace(OrigWidth, OrigHeight), AVCOL_RANGE_UNSPECIFIED,
		vi.width, vi.height, ConvertToFormat, GetAssumedColorSpace(OrigWidth, OrigHeight), AVCOL_RANGE_UNSPECIFIED,
		AvisynthToSWSCPUFlags(Env->GetCPUFlags()) | Resizer);
	if (Context == NULL)
		Env->ThrowError("SWScale: Context creation failed");
}

SWScale::~SWScale() {
	if (Context)
		sws_freeContext(Context);
}

PVideoFrame SWScale::GetFrame(int n, IScriptEnvironment *Env) {
	PVideoFrame Src = child->GetFrame(n, Env);
	PVideoFrame Dst = Env->NewVideoFrame(vi);

	const uint8_t *SrcData[3] = {(uint8_t *)Src->GetReadPtr(PLANAR_Y), (uint8_t *)Src->GetReadPtr(PLANAR_U), (uint8_t *)Src->GetReadPtr(PLANAR_V)};
	int SrcStride[3] = {Src->GetPitch(PLANAR_Y), Src->GetPitch(PLANAR_U), Src->GetPitch(PLANAR_V)};

	if (FlipOutput) {
		uint8_t *DstData[3] = {Dst->GetWritePtr(PLANAR_Y) + Dst->GetPitch(PLANAR_Y) * (Dst->GetHeight(PLANAR_Y) - 1), Dst->GetWritePtr(PLANAR_U) + Dst->GetPitch(PLANAR_U) * (Dst->GetHeight(PLANAR_U) - 1), Dst->GetWritePtr(PLANAR_V) + Dst->GetPitch(PLANAR_V) * (Dst->GetHeight(PLANAR_V) - 1)};
		int DstStride[3] = {-Dst->GetPitch(PLANAR_Y), -Dst->GetPitch(PLANAR_U), -Dst->GetPitch(PLANAR_V)};
		sws_scale(Context, SrcData, SrcStride, 0, OrigHeight, DstData, DstStride);
	} else {
		uint8_t *DstData[3] = {Dst->GetWritePtr(PLANAR_Y), Dst->GetWritePtr(PLANAR_U), Dst->GetWritePtr(PLANAR_V)};
		int DstStride[3] = {Dst->GetPitch(PLANAR_Y), Dst->GetPitch(PLANAR_U), Dst->GetPitch(PLANAR_V)};
		sws_scale(Context, SrcData, SrcStride, 0, OrigHeight, DstData, DstStride);
	}

	return Dst;
}
