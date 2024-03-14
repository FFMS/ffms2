//  Copyright (c) 2007-2017 Fredrik Mellbin
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

#ifndef FFAVSSOURCES_H
#define FFAVSSOURCES_H

#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include <avisynth.h>
#include "ffms.h"

struct ErrorInfo : FFMS_ErrorInfo {
    char ErrorBuffer[1024];
    ErrorInfo() {
        Buffer = ErrorBuffer;
        BufferSize = sizeof(ErrorBuffer);
    }
};

class AvisynthVideoSource : public IClip {
    VideoInfo VI;
    bool HighBitDepth;
    FFMS_VideoSource *V;
    int64_t FPSNum;
    int64_t FPSDen;
    const char *VarPrefix;
    bool has_at_least_v8;

    void InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
        const char *ResizerName, const char *ConvertToFormatName, IScriptEnvironment *Env);
    void OutputFrame(const FFMS_Frame *Frame, PVideoFrame &Dst, IScriptEnvironment *Env);
    void OutputField(const FFMS_Frame *Frame, PVideoFrame &Dst, int Field, IScriptEnvironment *Env);
public:
    AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
        int FPSNum, int FPSDen, int Threads, int SeekMode,
        int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
        const char *ConvertToFormatName, const char *VarPrefix, IScriptEnvironment* Env);
    ~AvisynthVideoSource();
    bool __stdcall GetParity(int n);
    int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; }
    const VideoInfo& __stdcall GetVideoInfo() { return VI; }
    void __stdcall GetAudio(void* Buf, int64_t Start, int64_t Count, IScriptEnvironment *Env) {}
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *Env);
};

class AvisynthAudioSource : public IClip {
    VideoInfo VI;
    FFMS_AudioSource *A;
public:
    AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index,
        int AdjustDelay, int FillGaps, double DrcScale, const char *VarPrefix, IScriptEnvironment* Env);
    ~AvisynthAudioSource();
    bool __stdcall GetParity(int n) { return false; }
    int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; }
    const VideoInfo& __stdcall GetVideoInfo() { return VI; }
    void __stdcall GetAudio(void* Buf, int64_t Start, int64_t Count, IScriptEnvironment *Env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *Env) { return nullptr; };
};

#endif
