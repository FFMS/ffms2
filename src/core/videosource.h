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

#ifndef FFVIDEOSOURCE_H
#define FFVIDEOSOURCE_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/stereo3d.h>
#include <libavutil/display.h>
}

#include <vector>

#include "track.h"
#include "utils.h"

struct FFMS_VideoSource {
private:
    SwsContext *SWS = nullptr;

    int LastFrameHeight;
    int LastFrameWidth;
    AVPixelFormat LastFramePixelFormat;

    int TargetHeight;
    int TargetWidth;
    std::vector<AVPixelFormat> TargetPixelFormats;
    int TargetResizer;

    AVPixelFormat OutputFormat;
    AVColorRange OutputColorRange;
    AVColorSpace OutputColorSpace;

    bool InputFormatOverridden;
    AVPixelFormat InputFormat;
    AVColorRange InputColorRange;
    AVColorSpace InputColorSpace;

    uint8_t *SWSFrameData[4] = {};
    int SWSFrameLinesize[4] = {};

    void DetectInputFormat();

    FFMS_VideoProperties VP;
    FFMS_Frame LocalFrame;
    AVFrame *DecodeFrame = nullptr;
    AVFrame *LastDecodedFrame = nullptr;
    int LastFrameNum;
    FFMS_Index &Index;
    FFMS_Track Frames;
    int VideoTrack;
    int CurrentFrame;
    int DelayCounter;
    int InitialDecode;
    int DecodingThreads;
    AVCodecContext *CodecContext = nullptr;
    AVFormatContext *FormatContext = nullptr;
    int SeekMode;
    bool SeekByPos = false;
    int PosOffset = 0;

    void ReAdjustOutputFormat();
    FFMS_Frame *OutputFrame(AVFrame *Frame);
    void SetVideoProperties();
    bool DecodePacket(AVPacket *Packet);
    void FlushFinalFrames();
    bool HasPendingDelayedFrames();
    void DecodeNextFrame(int64_t &PTS, int64_t &Pos);
    bool SeekTo(int n, int SeekOffset);
    int Seek(int n);
    int ReadFrame(AVPacket *pkt);
    void Free();
public:
    FFMS_VideoSource(const char *SourceFile, FFMS_Index &Index, int Track, int Threads, int SeekMode);
    ~FFMS_VideoSource();
    const FFMS_VideoProperties& GetVideoProperties() { return VP; }
    FFMS_Track *GetTrack() { return &Frames; }
    FFMS_Frame *GetFrame(int n);
    void GetFrameCheck(int n);
    FFMS_Frame *GetFrameByTime(double Time);
    void SetOutputFormat(const AVPixelFormat *TargetFormats, int Width, int Height, int Resizer);
    void ResetOutputFormat();
    void SetInputFormat(int ColorSpace, int ColorRange, AVPixelFormat Format);
    void ResetInputFormat();
};

#endif
