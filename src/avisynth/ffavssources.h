//  Copyright (c) 2007-2009 Fredrik Mellbin
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

#include <windows.h>
#include "avisynth.h"
#include "ffms.h"

class AvisynthVideoSource : public IClip {
private:
	VideoInfo VI;
	FFMS_VideoSource *V;
	int FPSNum;
	int FPSDen;

	void InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
		const char *ResizerName, const char *ConvertToFormatName,IScriptEnvironment *Env);
	PVideoFrame OutputFrame(const FFMS_Frame *SrcPicture, IScriptEnvironment *Env);
public:
	AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, const char *PP, int Threads, int SeekMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
		const char *ConvertToFormatName, IScriptEnvironment* Env);
	~AvisynthVideoSource();
	bool __stdcall GetParity(int n) { return false; }
	void __stdcall SetCacheHints(int cachehints, int frame_range) { }
	const VideoInfo& __stdcall GetVideoInfo() { return VI; }
	void __stdcall GetAudio(void* Buf, __int64 Start, __int64 Count, IScriptEnvironment *Env) { }
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *Env);
};

class AvisynthAudioSource : public IClip {
private:
	VideoInfo VI;
	FFMS_AudioSource *A;
public:
	AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index, IScriptEnvironment* Env);
	~AvisynthAudioSource();
	bool __stdcall GetParity(int n) { return false; }
	void __stdcall SetCacheHints(int cachehints, int frame_range) { }
	const VideoInfo& __stdcall GetVideoInfo() { return VI; }
	void __stdcall GetAudio(void* Buf, __int64 Start, __int64 Count, IScriptEnvironment *Env);
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *Env) { return NULL; };
};

#endif
