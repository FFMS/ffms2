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

#ifndef FFSWSCALE_H
#define FFSWSCALE_H

extern "C" {
#include <libswscale/swscale.h>
}

#include "ffmscompat.h"
#include "avxplugin.h"

SwsContext *FFGetSwsContext(int SrcW, int SrcH, PixelFormat SrcFormat, int DstW, int DstH, PixelFormat DstFormat, int64_t Flags, int ColorSpace = SWS_CS_DEFAULT, int ColorRange = -1);
int FFGetSwsAssumedColorSpace(int Width, int Height);

class SWScale : public avxsynth::GenericVideoFilter {
private:
	SwsContext *Context;
	int OrigWidth;
	int OrigHeight;
	bool FlipOutput;
public:
	SWScale(avxsynth::PClip Child, int ResizeToWidth, int ResizeToHeight, const char *ResizerName, const char *ConvertToFormatName, avxsynth::IScriptEnvironment *Env);
	~SWScale();
    avxsynth::PVideoFrame __stdcall GetFrame(int n, avxsynth::IScriptEnvironment *Env);
};

#endif
