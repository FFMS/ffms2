//  Copyright (c) 2009 Fredrik Mellbin
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

#include <windows.h>
#include "avisynth.h"
#include "ffms.h"

extern "C" {
#include <libswscale/swscale.h>
}



int64_t AvisynthToFFCPUFlags(long AvisynthFlags) {
	int64_t Flags = 0;

	if (AvisynthFlags & CPUF_MMX)
		Flags |= SWS_CPU_CAPS_MMX;
	if (AvisynthFlags & CPUF_INTEGER_SSE)
		Flags |= SWS_CPU_CAPS_MMX2;
	if (AvisynthFlags & CPUF_3DNOW_EXT)
		Flags |= SWS_CPU_CAPS_3DNOW;
#ifdef SWS_CPU_CAPS_SSE2
	if (AvisynthFlags & CPUF_SSE2)
		Flags |= SWS_CPU_CAPS_SSE2;
#endif

	return Flags;
}

PixelFormat CSNameToPIXFMT(const char *CSName, PixelFormat Default) {
	if (!_stricmp(CSName, ""))
		return Default;
	if (!_stricmp(CSName, "YV12"))
		return PIX_FMT_YUV420P;
	if (!_stricmp(CSName, "YUY2"))
		return PIX_FMT_YUYV422;
	if (!_stricmp(CSName, "RGB24"))
		return PIX_FMT_BGR24;
	if (!_stricmp(CSName, "RGB32"))
		return PIX_FMT_RGB32;
	return PIX_FMT_NONE;
}

int ResizerNameToSWSResizer(const char *ResizerName) {
	if (!_stricmp(ResizerName, "FAST_BILINEAR"))
		return SWS_FAST_BILINEAR;
	if (!_stricmp(ResizerName, "BILINEAR"))
		return SWS_BILINEAR;
	if (!_stricmp(ResizerName, "BICUBIC"))
		return SWS_BICUBIC;
	if (!_stricmp(ResizerName, "X"))
		return SWS_X;
	if (!_stricmp(ResizerName, "POINT"))
		return SWS_POINT;
	if (!_stricmp(ResizerName, "AREA"))
		return SWS_AREA;
	if (!_stricmp(ResizerName, "BICUBLIN"))
		return SWS_BICUBLIN;
	if (!_stricmp(ResizerName, "GAUSS"))
		return SWS_GAUSS;
	if (!_stricmp(ResizerName, "SINC"))
		return SWS_SINC;
	if (!_stricmp(ResizerName, "LANCZOS"))
		return SWS_LANCZOS;
	if (!_stricmp(ResizerName, "SPLINE"))
		return SWS_SPLINE;
	return 0;
}
