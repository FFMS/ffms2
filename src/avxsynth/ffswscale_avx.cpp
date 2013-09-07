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

#include "ffswscale_avx.h"
#include "avsutils_avx.h"
extern "C" {
#include <libavutil/opt.h>
}

static int64_t AvisynthToSWSCPUFlags(long AvisynthFlags) {
	int64_t Flags = 0;
#ifdef SWS_CPU_CAPS_MMX
	if (AvisynthFlags & avxsynth::CPUF_MMX)
		Flags |= SWS_CPU_CAPS_MMX;
	if (AvisynthFlags & avxsynth::CPUF_INTEGER_SSE)
		Flags |= SWS_CPU_CAPS_MMX2;
	if (AvisynthFlags & avxsynth::CPUF_3DNOW_EXT)
		Flags |= SWS_CPU_CAPS_3DNOW;
	if (AvisynthFlags & avxsynth::CPUF_SSE2)
		Flags |= SWS_CPU_CAPS_SSE2;
#endif
	return Flags;
}


SwsContext *FFGetSwsContext(int SrcW, int SrcH, PixelFormat SrcFormat, int DstW, int DstH, PixelFormat DstFormat, int64_t Flags, int ColorSpace, int ColorRange) {
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

    av_opt_set_int(Context, "sws_flags", Flags, 0);
    av_opt_set_int(Context, "srcw",       SrcW, 0);
    av_opt_set_int(Context, "srch",       SrcH, 0);
    av_opt_set_int(Context, "dstw",       DstW, 0);
    av_opt_set_int(Context, "dsth",       DstH, 0);
    av_opt_set_int(Context, "src_range",  Range, 0);
    av_opt_set_int(Context, "dst_range",  Range, 0);
    av_opt_set_int(Context, "src_format", SrcFormat, 0);
    av_opt_set_int(Context, "dst_format", DstFormat, 0);

    sws_setColorspaceDetails(Context, sws_getCoefficients(ColorSpace), Range, sws_getCoefficients(ColorSpace), Range, 0, 1<<16, 1<<16);

    if(sws_init_context(Context, 0, 0) < 0){
        sws_freeContext(Context);
        return 0;
    }

    return Context;
#endif

}

int FFGetSwsAssumedColorSpace(int W, int H) {
    if (W > 1024 || H >= 600)
        return SWS_CS_ITU709;
    else
        return SWS_CS_DEFAULT;
}

SWScale::SWScale(avxsynth::PClip Child, int ResizeToWidth, int ResizeToHeight, const char *ResizerName, const char *ConvertToFormatName, avxsynth::IScriptEnvironment *Env)
  : avxsynth::GenericVideoFilter(Child) {
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
        case PIX_FMT_YUV420P: vi.pixel_type = avxsynth::VideoInfo::CS_I420; break;
        case PIX_FMT_YUYV422: vi.pixel_type = avxsynth::VideoInfo::CS_YUY2; break;
        case PIX_FMT_BGR24: vi.pixel_type = avxsynth::VideoInfo::CS_BGR24; break;
        case PIX_FMT_RGB32: vi.pixel_type = avxsynth::VideoInfo::CS_BGR32; break;
        case PIX_FMT_NONE:
        case PIX_FMT_RGB24:
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUV410P:
        case PIX_FMT_YUV411P:
        case PIX_FMT_GRAY8:
        case PIX_FMT_MONOWHITE:
        case PIX_FMT_MONOBLACK:
        case PIX_FMT_PAL8:
        case PIX_FMT_YUVJ420P:
        case PIX_FMT_YUVJ422P:
        case PIX_FMT_YUVJ444P:
        case PIX_FMT_XVMC_MPEG2_MC:
        case PIX_FMT_XVMC_MPEG2_IDCT:
        case PIX_FMT_UYVY422:
        case PIX_FMT_UYYVYY411:
        case PIX_FMT_BGR8:
        case PIX_FMT_BGR4:
        case PIX_FMT_BGR4_BYTE:
        case PIX_FMT_RGB8:
        case PIX_FMT_RGB4:
        case PIX_FMT_RGB4_BYTE:
        case PIX_FMT_NV12:
        case PIX_FMT_NV21:
        case PIX_FMT_ARGB:
        case PIX_FMT_RGBA:
        case PIX_FMT_ABGR:
        case PIX_FMT_GRAY16BE:
        case PIX_FMT_GRAY16LE:
        case PIX_FMT_YUV440P:
        case PIX_FMT_YUVJ440P:
        case PIX_FMT_YUVA420P:
        case PIX_FMT_VDPAU_H264:
        case PIX_FMT_VDPAU_MPEG1:
        case PIX_FMT_VDPAU_MPEG2:
        case PIX_FMT_VDPAU_WMV3:
        case PIX_FMT_VDPAU_VC1:
        case PIX_FMT_RGB48BE:
        case PIX_FMT_RGB48LE:
        case PIX_FMT_RGB565BE:
        case PIX_FMT_RGB565LE:
        case PIX_FMT_RGB555BE:
        case PIX_FMT_RGB555LE:
        case PIX_FMT_BGR565BE:
        case PIX_FMT_BGR565LE:
        case PIX_FMT_BGR555BE:
        case PIX_FMT_BGR555LE:
        case PIX_FMT_VAAPI_MOCO:
        case PIX_FMT_VAAPI_IDCT:
        case PIX_FMT_VAAPI_VLD:
        case PIX_FMT_YUV420P16LE:
        case PIX_FMT_YUV420P16BE:
        case PIX_FMT_YUV422P16LE:
        case PIX_FMT_YUV422P16BE:
        case PIX_FMT_YUV444P16LE:
        case PIX_FMT_YUV444P16BE:
        case PIX_FMT_VDPAU_MPEG4:
        case PIX_FMT_DXVA2_VLD:
        case PIX_FMT_RGB444LE:
        case PIX_FMT_RGB444BE:
        case PIX_FMT_BGR444LE:
        case PIX_FMT_BGR444BE:
        case PIX_FMT_Y400A:
        case PIX_FMT_BGR48BE:
        case PIX_FMT_BGR48LE:
        case PIX_FMT_YUV420P9BE:
        case PIX_FMT_YUV420P9LE:
        case PIX_FMT_YUV420P10BE:
        case PIX_FMT_YUV420P10LE:
        case PIX_FMT_YUV422P10BE:
        case PIX_FMT_YUV422P10LE:
        case PIX_FMT_YUV444P9BE:
        case PIX_FMT_YUV444P9LE:
        case PIX_FMT_YUV444P10BE:
        case PIX_FMT_YUV444P10LE:
        case PIX_FMT_NB:
        default:
            break;
	}

	FlipOutput ^= vi.IsYUV();

	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		Env->ThrowError("SWScale: Invalid resizer specified (%s)", ResizerName);

	if (ConvertToFormat == PIX_FMT_YUV420P && vi.height & 1)
		Env->ThrowError("SWScale: mod 2 output height required");

	if ((ConvertToFormat == PIX_FMT_YUV420P || ConvertToFormat == PIX_FMT_YUYV422) && vi.width & 1)
		Env->ThrowError("SWScale: mod 2 output width required");

	Context = FFGetSwsContext(OrigWidth, OrigHeight, ConvertFromFormat, vi.width, vi.height, ConvertToFormat,
		AvisynthToSWSCPUFlags(Env->GetCPUFlags()) | Resizer, FFGetSwsAssumedColorSpace(OrigWidth, OrigHeight));
	if (Context == NULL)
		Env->ThrowError("SWScale: Context creation failed");
}

SWScale::~SWScale() {
	if (Context)
		sws_freeContext(Context);
}

avxsynth::PVideoFrame SWScale::GetFrame(int n, avxsynth::IScriptEnvironment *Env) {
	avxsynth::PVideoFrame Src = child->GetFrame(n, Env);
	avxsynth::PVideoFrame Dst = Env->NewVideoFrame(vi);

	const uint8_t *SrcData[3] = {(uint8_t *)Src->GetReadPtr(avxsynth::PLANAR_Y), (uint8_t *)Src->GetReadPtr(avxsynth::PLANAR_U), (uint8_t *)Src->GetReadPtr(avxsynth::PLANAR_V)};
	int SrcStride[3] = {Src->GetPitch(avxsynth::PLANAR_Y), Src->GetPitch(avxsynth::PLANAR_U), Src->GetPitch(avxsynth::PLANAR_V)};

	if (FlipOutput) {
		uint8_t *DstData[3] = {Dst->GetWritePtr(avxsynth::PLANAR_Y) + Dst->GetPitch(avxsynth::PLANAR_Y) * (Dst->GetHeight(avxsynth::PLANAR_Y) - 1), Dst->GetWritePtr(avxsynth::PLANAR_U) + Dst->GetPitch(avxsynth::PLANAR_U) * (Dst->GetHeight(avxsynth::PLANAR_U) - 1), Dst->GetWritePtr(avxsynth::PLANAR_V) + Dst->GetPitch(avxsynth::PLANAR_V) * (Dst->GetHeight(avxsynth::PLANAR_V) - 1)};
		int DstStride[3] = {-Dst->GetPitch(avxsynth::PLANAR_Y), -Dst->GetPitch(avxsynth::PLANAR_U), -Dst->GetPitch(avxsynth::PLANAR_V)};
		sws_scale(Context, SrcData, SrcStride, 0, OrigHeight, DstData, DstStride);
	} else {
		uint8_t *DstData[3] = {Dst->GetWritePtr(avxsynth::PLANAR_Y), Dst->GetWritePtr(avxsynth::PLANAR_U), Dst->GetWritePtr(avxsynth::PLANAR_V)};
		int DstStride[3] = {Dst->GetPitch(avxsynth::PLANAR_Y), Dst->GetPitch(avxsynth::PLANAR_U), Dst->GetPitch(avxsynth::PLANAR_V)};
		sws_scale(Context, SrcData, SrcStride, 0, OrigHeight, DstData, DstStride);
	}

	return Dst;
}
