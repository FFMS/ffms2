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

extern "C" {
#include <libavutil/mem.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
}
// must be included after ffmpeg headers
#include "ffmscompat.h"

#include <vector>

#include "ffms.h"


// swscale and pp-related functions
int64_t GetSWSCPUFlags();
SwsContext *GetSwsContext(int SrcW, int SrcH, PixelFormat SrcFormat, int SrcColorSpace, int SrcColorRange, int DstW, int DstH, PixelFormat DstFormat, int DstColorSpace, int DstColorRange, int64_t Flags);
AVColorSpace GetAssumedColorSpace(int Width, int Height);

// timebase-related functions
void CorrectNTSCRationalFramerate(int *Num, int *Den);
void CorrectTimebase(FFMS_VideoProperties *VP, FFMS_TrackTimeBase *TTimebase);

// our implementation of avcodec_find_best_pix_fmt()
PixelFormat FindBestPixelFormat(const std::vector<PixelFormat> &Dsts, PixelFormat Src);

void RegisterCustomParsers();
