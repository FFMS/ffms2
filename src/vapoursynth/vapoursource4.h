//  Copyright (c) 2012-2017 Fredrik Mellbin
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

#ifndef FFVAPOURSOURCES_H
#define FFVAPOURSOURCES_H

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/common.h>
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "VapourSynth4.h"
#include "ffms.h"
#include "ffmscompat.h"

struct VSVideoSource4 {
private:
    VSVideoInfo VI[2];
    FFMS_VideoSource *V;
    int64_t FPSNum;
    int64_t FPSDen;
    int SARNum;
    int SARDen;
    bool OutputAlpha;
    int LastFrame = -1;
    int CacheThreshold = 0;

    void InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
        const char *ResizerName, int ConvertToFormat, const VSAPI *vsapi, VSCore *core);
    static void OutputFrame(const FFMS_Frame *Frame, VSFrame *Dst, const VSAPI *vsapi);
    static void OutputAlphaFrame(const FFMS_Frame *Frame, int Plane, VSFrame *Dst, const VSAPI *vsapi);
public:
    VSVideoSource4(const char *SourceFile, int Track, FFMS_Index *Index,
        int AFPSNum, int AFPSDen, int Threads, int SeekMode,
        int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
        int Format, bool OutputAlpha, const VSAPI *vsapi, VSCore *core);
    ~VSVideoSource4();

    const VSVideoInfo *GetVideoInfo() const;
    void SetCacheThreshold(int threshold);

    static void VS_CC Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi);
    const VSFrame *VS_CC GetVSFrame(int n, VSCore *core, const VSAPI *vsapi);
    static const VSFrame *VS_CC GetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
    static void VS_CC Free(void *instanceData, VSCore *core, const VSAPI *vsapi);
};

#endif
