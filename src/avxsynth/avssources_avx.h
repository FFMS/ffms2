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

#ifndef FFAVSSOURCES_H
#define FFAVSSOURCES_H

#include <vector>
#include "avxplugin.h"
#include "ffms.h"



class AvisynthVideoSource : public avxsynth::IClip {
private:
	struct FrameFields {
		int Top;
		int Bottom;
	};

	avxsynth::VideoInfo VI;
	FFMS_VideoSource *V;
	int FPSNum;
	int FPSDen;
	int RFFMode;
	std::vector<FrameFields> FieldList;
	const char *VarPrefix;

	void InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
		const char *ResizerName, const char *ConvertToFormatName, avxsynth::IScriptEnvironment *Env);
	void OutputFrame(const FFMS_Frame *Frame, avxsynth::PVideoFrame &Dst, avxsynth::IScriptEnvironment *Env);
	void OutputField(const FFMS_Frame *Frame, avxsynth::PVideoFrame &Dst, int Field, avxsynth::IScriptEnvironment *Env);
public:
	AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, int Threads, int SeekMode, int RFFMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
        const char *ConvertToFormatName, const char *VarPrefix, avxsynth::IScriptEnvironment* Env);
	~AvisynthVideoSource();
	bool __stdcall GetParity(int n);
	void __stdcall SetCacheHints(int cachehints, size_t frame_range) { }
	const avxsynth::VideoInfo& __stdcall GetVideoInfo() { return VI; }
	void __stdcall GetAudio(void* Buf, avxsynth::__int64 Start, avxsynth::__int64 Count, avxsynth::IScriptEnvironment *Env) { }
	avxsynth::PVideoFrame __stdcall GetFrame(int n, avxsynth::IScriptEnvironment *Env);
};

class AvisynthAudioSource : public avxsynth::IClip {
private:
	avxsynth::VideoInfo VI;
	FFMS_AudioSource *A;
public:
	AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int AdjustDelay, const char *VarPrefix, avxsynth::IScriptEnvironment* Env);
	~AvisynthAudioSource();
	bool __stdcall GetParity(int n) { return false; }
	void __stdcall SetCacheHints(int cachehints, size_t frame_range) { }
	const avxsynth::VideoInfo& __stdcall GetVideoInfo() { return VI; }
	void __stdcall GetAudio(void* Buf, avxsynth::__int64 Start, avxsynth::__int64 Count, avxsynth::IScriptEnvironment *Env);
	avxsynth::PVideoFrame __stdcall GetFrame(int n, avxsynth::IScriptEnvironment *Env) { return NULL; };
};

#endif
