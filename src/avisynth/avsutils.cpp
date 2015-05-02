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

#include "avsutils.h"
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
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
