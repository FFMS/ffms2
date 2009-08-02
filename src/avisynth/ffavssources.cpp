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

#include "ffavssources.h"
#include "avsutils.h"
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
}

AvisynthVideoSource::AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, const char *PP, int Threads, int SeekMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
		const char *ConvertToFormatName, IScriptEnvironment* Env, char *ErrorMsg, unsigned MsgSize) {
	memset(&VI, 0, sizeof(VI));
	this->FPSNum = FPSNum;
	this->FPSDen = FPSDen;

	V = FFMS_CreateVideoSource(SourceFile, Track, Index, PP, Threads, SeekMode, NULL, ErrorMsg, MsgSize);
	if (!V)
		Env->ThrowError(ErrorMsg);

	try {
		InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, ConvertToFormatName, Env);
	} catch (AvisynthError &) {
		FFMS_DestroyVideoSource(V);
		throw;
	}

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

	if (FPSNum > 0 && FPSDen > 0) {
		VI.fps_denominator = FPSDen;
		VI.fps_numerator = FPSNum;
		VI.num_frames = static_cast<int>(ceil(((VP->LastTime - VP->FirstTime) * FPSNum) / FPSDen));
	} else {
		VI.fps_denominator = VP->FPSDenominator;
		VI.fps_numerator = VP->FPSNumerator;
		VI.num_frames = VP->NumFrames;
	}

	// Set AR variables
	Env->SetVar("FFSAR_NUM", VP->SARNum);
	Env->SetVar("FFSAR_DEN", VP->SARDen);
	if (VP->SARNum > 0 && VP->SARDen > 0)
		Env->SetVar("FFSAR", VP->SARNum / (double)VP->SARDen);

	// Set crop variables
	Env->SetVar("FFCROP_LEFT", VP->CropLeft);
	Env->SetVar("FFCROP_RIGHT", VP->CropRight);
	Env->SetVar("FFCROP_TOP", VP->CropTop);
	Env->SetVar("FFCROP_BOTTOM", VP->CropBottom);
}

AvisynthVideoSource::~AvisynthVideoSource() {
	FFMS_DestroyVideoSource(V);
}

void AvisynthVideoSource::InitOutputFormat(
	int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
	const char *ConvertToFormatName, IScriptEnvironment *Env) {

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

	int64_t TargetFormats = (1 << FFMS_GetPixFmt("yuvj420p")) |
		(1 << FFMS_GetPixFmt("yuv420p")) | (1 << FFMS_GetPixFmt("yuyv422")) |
		(1 << FFMS_GetPixFmt("rgb32")) | (1 << FFMS_GetPixFmt("bgr24"));

	// PIX_FMT_NV12 is misused as a return value different to the defined ones in the function
	PixelFormat TargetPixelFormat = CSNameToPIXFMT(ConvertToFormatName, PIX_FMT_NV12);
	if (TargetPixelFormat == PIX_FMT_NONE)
		Env->ThrowError("FFVideoSource: Invalid colorspace name specified");

	if (TargetPixelFormat != PIX_FMT_NV12)
		TargetFormats = static_cast<int64_t>(1) << TargetPixelFormat;

	if (ResizeToWidth <= 0)
		ResizeToWidth = VP->Width;

	if (ResizeToHeight <= 0)
		ResizeToHeight = VP->Height;

	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		Env->ThrowError("FFVideoSource: Invalid resizer name specified");

	if (FFMS_SetOutputFormatV(V, TargetFormats,
		ResizeToWidth, ResizeToHeight, Resizer, NULL, 0))
		Env->ThrowError("FFVideoSource: No suitable output format found");

	VP = FFMS_GetVideoProperties(V);

	// This trick is required to first get the "best" default format and then set only that format as the output
	if (FFMS_SetOutputFormatV(V, static_cast<int64_t>(1) << VP->VPixelFormat,
		ResizeToWidth, ResizeToHeight, Resizer, NULL, 0))
		Env->ThrowError("FFVideoSource: No suitable output format found");

	VP = FFMS_GetVideoProperties(V);

	if (VP->VPixelFormat == FFMS_GetPixFmt("yuvj420p") || VP->VPixelFormat == FFMS_GetPixFmt("yuv420p"))
		VI.pixel_type = VideoInfo::CS_I420;
	else if (VP->VPixelFormat == FFMS_GetPixFmt("yuyv422"))
		VI.pixel_type = VideoInfo::CS_YUY2;
	else if (VP->VPixelFormat == FFMS_GetPixFmt("rgb32"))
		VI.pixel_type = VideoInfo::CS_BGR32;
	else if (VP->VPixelFormat == FFMS_GetPixFmt("bgr24"))
		VI.pixel_type = VideoInfo::CS_BGR24;
	else
		Env->ThrowError("FFVideoSource: No suitable output format found");

	VI.image_type = VideoInfo::IT_TFF;
	VI.width = VP->Width;
	VI.height = VP->Height;

	// Crop to obey avisynth's even width/height requirements
	if (VI.pixel_type == VideoInfo::CS_I420) {
		VI.height -= VI.height & 1;
		VI.width -= VI.width & 1;
	}

	if (VI.pixel_type == VideoInfo::CS_YUY2) {
		VI.width -= VI.width & 1;
	}
}

PVideoFrame AvisynthVideoSource::OutputFrame(const FFMS_Frame *Frame, IScriptEnvironment *Env) {
	FFMS_Frame *SrcPicture = const_cast<FFMS_Frame *>(Frame);
	PVideoFrame Dst = Env->NewVideoFrame(VI);

	if (VI.pixel_type == VideoInfo::CS_I420) {
		Env->BitBlt(Dst->GetWritePtr(PLANAR_Y), Dst->GetPitch(PLANAR_Y), SrcPicture->Data[0], SrcPicture->Linesize[0], Dst->GetRowSize(PLANAR_Y), Dst->GetHeight(PLANAR_Y));
		Env->BitBlt(Dst->GetWritePtr(PLANAR_U), Dst->GetPitch(PLANAR_U), SrcPicture->Data[1], SrcPicture->Linesize[1], Dst->GetRowSize(PLANAR_U), Dst->GetHeight(PLANAR_U));
		Env->BitBlt(Dst->GetWritePtr(PLANAR_V), Dst->GetPitch(PLANAR_V), SrcPicture->Data[2], SrcPicture->Linesize[2], Dst->GetRowSize(PLANAR_V), Dst->GetHeight(PLANAR_V));
	} else if (VI.IsRGB()) {
		Env->BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch(), SrcPicture->Data[0], SrcPicture->Linesize[0], Dst->GetRowSize(), Dst->GetHeight());
	} else { // YUY2
		Env->BitBlt(Dst->GetWritePtr(), Dst->GetPitch(), SrcPicture->Data[0], SrcPicture->Linesize[0], Dst->GetRowSize(), Dst->GetHeight());
	}

	return Dst;
}

PVideoFrame AvisynthVideoSource::GetFrame(int n, IScriptEnvironment *Env) {
	char ErrorMsg[1024];
	unsigned MsgSize = sizeof(ErrorMsg);
	const FFMS_Frame *Frame;

	if (FPSNum > 0 && FPSDen > 0)
		Frame = FFMS_GetFrameByTime(V, FFMS_GetVideoProperties(V)->FirstTime +
		(double)(n * (int64_t)FPSDen) / FPSNum, NULL, ErrorMsg, MsgSize);
	else
		Frame = FFMS_GetFrame(V, n, NULL, ErrorMsg, MsgSize);

	if (Frame == NULL)
		Env->ThrowError("FFVideoSource: %s", ErrorMsg);

	Env->SetVar("FFPICT_TYPE", static_cast<int>(Frame->PictType));
	return OutputFrame(Frame, Env);
}

AvisynthAudioSource::AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index, IScriptEnvironment* Env, char *ErrorMsg, unsigned MsgSize) {
	memset(&VI, 0, sizeof(VI));

	A = FFMS_CreateAudioSource(SourceFile, Track, Index, NULL, ErrorMsg, MsgSize);
	if (!A)
		Env->ThrowError(ErrorMsg);

	const FFMS_AudioProperties *AP = FFMS_GetAudioProperties(A);
	VI.nchannels = AP->Channels;
	VI.num_audio_samples = AP->NumSamples;
	VI.audio_samples_per_second = AP->SampleRate;

	switch (AP->SampleFormat) {
		case FFMS_FMT_U8: VI.sample_type = SAMPLE_INT8; break;
		case FFMS_FMT_S16: VI.sample_type = SAMPLE_INT16; break;
		case FFMS_FMT_S32: VI.sample_type = SAMPLE_INT32; break;
		case FFMS_FMT_FLT: VI.sample_type = SAMPLE_FLOAT; break;
		default: Env->ThrowError("FFAudioSource: Bad audio format");
	}
}

AvisynthAudioSource::~AvisynthAudioSource() {
	FFMS_DestroyAudioSource(A);
}

void AvisynthAudioSource::GetAudio(void* Buf, __int64 Start, __int64 Count, IScriptEnvironment *Env) {
	char ErrorMsg[1024];
	unsigned MsgSize = sizeof(ErrorMsg);
	if (FFMS_GetAudio(A, Buf, Start, Count, ErrorMsg, MsgSize))
		Env->ThrowError(ErrorMsg);
}
