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

#include "avssources_avx.h"
#include "avsutils_avx.h"
#include <libavutil/common.h>
#include <cmath>



AvisynthVideoSource::AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, int Threads, int SeekMode, int RFFMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
		const char *ConvertToFormatName, const char *VarPrefix, avxsynth::IScriptEnvironment* Env) {

	memset(&VI, 0, sizeof(VI));
	this->FPSNum = FPSNum;
	this->FPSDen = FPSDen;
	this->RFFMode = RFFMode;
	this->VarPrefix = VarPrefix;

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	V = FFMS_CreateVideoSource(SourceFile, Track, Index, Threads, SeekMode, &E);
	if (!V)
		Env->ThrowError("FFVideoSource: %s", E.Buffer);

	try {
		InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, ConvertToFormatName, Env);
	} catch (avxsynth::AvisynthError &) {
		FFMS_DestroyVideoSource(V);
		throw;
	}

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

	if (RFFMode > 0) {
		// This part assumes things, and so should you

		FFMS_Track *VTrack = FFMS_GetTrackFromVideo(V);

		if (FFMS_GetFrameInfo(VTrack, 0)->RepeatPict < 0) {
			FFMS_DestroyVideoSource(V);
			Env->ThrowError("FFVideoSource: No RFF flags present");
		}

		int RepeatMin = FFMS_GetFrameInfo(VTrack, 0)->RepeatPict;;
		int NumFields = 0;

		for (int i = 0; i < VP->NumFrames; i++) {
			int RepeatPict = FFMS_GetFrameInfo(VTrack, i)->RepeatPict;
			NumFields += RepeatPict + 1;
			RepeatMin = FFMIN(RepeatMin, RepeatPict);
		}

		for (int i = 0; i < VP->NumFrames; i++) {
			int RepeatPict = FFMS_GetFrameInfo(VTrack, i)->RepeatPict;

			if (((RepeatPict + 1) * 2) % (RepeatMin + 1)) {
				FFMS_DestroyVideoSource(V);
				Env->ThrowError("FFVideoSource: Unsupported RFF flag pattern");
			}
		}

		VI.fps_denominator = VP->RFFDenominator * (RepeatMin + 1);
		VI.fps_numerator = VP->RFFNumerator;
		VI.num_frames = (NumFields + RepeatMin) / (RepeatMin + 1);

		int DestField = 0;
		FieldList.resize(VI.num_frames);
		for (int i = 0; i < VP->NumFrames; i++) {
			int RepeatPict = FFMS_GetFrameInfo(VTrack, i)->RepeatPict;
			int RepeatFields = ((RepeatPict + 1) * 2) / (RepeatMin + 1);

			for (int j = 0; j < RepeatFields; j++) {
				if ((DestField + (VP->TopFieldFirst ? 0 : 1)) & 1)
					FieldList[DestField / 2].Top = i;
				else
					FieldList[DestField / 2].Bottom = i;
				DestField++;
			}
		}

		if (RFFMode == 2) {
			VI.num_frames = (VI.num_frames * 4) / 5;
			VI.fps_denominator *= 5;
			VI.fps_numerator *= 4;

			int OutputFrames = 0;

			for (int i = 0; i < VI.num_frames / 4; i++) {
				bool HasDropped = false;

				FieldList[OutputFrames].Top = FieldList[i * 5].Top;
				FieldList[OutputFrames].Bottom = FieldList[i * 5].Top;
				OutputFrames++;

				for (int j = 1; j < 5; j++) {
					if (!HasDropped && FieldList[i * 5 + j - 1].Top == FieldList[i * 5 + j].Top) {
						HasDropped = true;
						continue;
					}

					FieldList[OutputFrames].Top = FieldList[i * 5 + j].Top;
					FieldList[OutputFrames].Bottom = FieldList[i * 5 + j].Top;
					OutputFrames++;
				}

				if (!HasDropped)
					OutputFrames--;
			}

			if (OutputFrames > 0)
				for (int i = OutputFrames - 1; i < static_cast<int>(FieldList.size()); i++) {
						FieldList[i].Top = FieldList[OutputFrames - 1].Top;
						FieldList[i].Bottom = FieldList[OutputFrames - 1].Top;
				}

			FieldList.resize(VI.num_frames);
		}
	} else {
		if (FPSNum > 0 && FPSDen > 0) {
			VI.fps_denominator = FPSDen;
			VI.fps_numerator = FPSNum;
			if (VP->NumFrames > 1) {
				VI.num_frames = static_cast<int>((VP->LastTime - VP->FirstTime) * (1 + 1. / (VP->NumFrames - 1)) * FPSNum / FPSDen + 0.5);
				if (VI.num_frames < 1) VI.num_frames = 1;
			} else {
				VI.num_frames = 1;
			}
		} else {
			VI.fps_denominator = VP->FPSDenominator;
			VI.fps_numerator = VP->FPSNumerator;
			VI.num_frames = VP->NumFrames;
		}
	}

	// Set AR variables
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFSAR_NUM"), VP->SARNum);
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFSAR_DEN"), VP->SARDen);
	if (VP->SARNum > 0 && VP->SARDen > 0)
		Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFSAR"), VP->SARNum / (double)VP->SARDen);

	// Set crop variables
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCROP_LEFT"), VP->CropLeft);
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCROP_RIGHT"), VP->CropRight);
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCROP_TOP"), VP->CropTop);
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCROP_BOTTOM"), VP->CropBottom);

	Env->SetGlobalVar("FFVAR_PREFIX", this->VarPrefix);
}

AvisynthVideoSource::~AvisynthVideoSource() {
	FFMS_DestroyVideoSource(V);
}

void AvisynthVideoSource::InitOutputFormat(
	int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
	const char *ConvertToFormatName, avxsynth::IScriptEnvironment *Env) {

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
	const FFMS_Frame *F = FFMS_GetFrame(V, 0, &E);
	if (!F)
		Env->ThrowError("FFVideoSource: %s", E.Buffer);

	int TargetFormats[4];
	TargetFormats[0] = FFMS_GetPixFmt("yuv420p");
	TargetFormats[1] = FFMS_GetPixFmt("yuyv422");
	TargetFormats[2] = FFMS_GetPixFmt("bgra");
	TargetFormats[3] = -1;

	// PIX_FMT_NV21 is misused as a return value different to the defined ones in the function
	PixelFormat TargetPixelFormat = CSNameToPIXFMT(ConvertToFormatName, PIX_FMT_NV21);
	if (TargetPixelFormat == PIX_FMT_NONE)
		Env->ThrowError("FFVideoSource: Invalid colorspace name specified");

	if (TargetPixelFormat != PIX_FMT_NV21) {
		TargetFormats[0] = TargetPixelFormat;
		TargetFormats[1] = -1;
	}

	if (ResizeToWidth <= 0)
		ResizeToWidth = F->EncodedWidth;

	if (ResizeToHeight <= 0)
		ResizeToHeight = F->EncodedHeight;

	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		Env->ThrowError("FFVideoSource: Invalid resizer name specified");

	if (FFMS_SetOutputFormatV2(V, TargetFormats,
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		Env->ThrowError("FFVideoSource: No suitable output format found");

	F = FFMS_GetFrame(V, 0, &E);
	TargetFormats[0] = F->ConvertedPixelFormat;
	TargetFormats[1] = -1;

		// This trick is required to first get the "best" default format and then set only that format as the output
	if (FFMS_SetOutputFormatV2(V, TargetFormats,
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		Env->ThrowError("FFVideoSource: No suitable output format found");

	F = FFMS_GetFrame(V, 0, &E);

	if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuvj420p") || F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv420p"))
		VI.pixel_type = avxsynth::VideoInfo::CS_I420;
	else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuyv422"))
		VI.pixel_type = avxsynth::VideoInfo::CS_YUY2;
	else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("rgb32"))
		VI.pixel_type = avxsynth::VideoInfo::CS_BGR32;
	else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("bgr24"))
		VI.pixel_type = avxsynth::VideoInfo::CS_BGR24;
	else
		Env->ThrowError("FFVideoSource: No suitable output format found");

	if (RFFMode > 0 && ResizeToHeight != F->EncodedHeight)
		Env->ThrowError("FFVideoSource: Vertical scaling not allowed in RFF mode");

	if (RFFMode > 0 && TargetPixelFormat != PIX_FMT_NV21)
		Env->ThrowError("FFVideoSource: Only the default output colorspace can be used in RFF mode");

	// set color information variables
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCOLOR_SPACE"), F->ColorSpace);
	Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCOLOR_RANGE"), F->ColorRange);

	if (VP->TopFieldFirst)
		VI.image_type = avxsynth::VideoInfo::IT_TFF;
	else
		VI.image_type = avxsynth::VideoInfo::IT_BFF;

	VI.width = F->ScaledWidth;
	VI.height = F->ScaledHeight;

	// Crop to obey avisynth's even width/height requirements
	if (VI.pixel_type == avxsynth::VideoInfo::CS_I420) {
		VI.height -= VI.height & 1;
		VI.width -= VI.width & 1;
	}

	if (VI.pixel_type == avxsynth::VideoInfo::CS_YUY2) {
		VI.width -= VI.width & 1;
	}

	if (RFFMode > 0) {
		VI.height -= VI.height & 1;
	}
}

void AvisynthVideoSource::OutputFrame(const FFMS_Frame *Frame, avxsynth::PVideoFrame &Dst, avxsynth::IScriptEnvironment *Env) {
	if (VI.pixel_type == avxsynth::VideoInfo::CS_I420) {
		Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_Y), Dst->GetPitch(avxsynth::PLANAR_Y), Frame->Data[0], Frame->Linesize[0], Dst->GetRowSize(avxsynth::PLANAR_Y), Dst->GetHeight(avxsynth::PLANAR_Y));
		Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_U), Dst->GetPitch(avxsynth::PLANAR_U), Frame->Data[1], Frame->Linesize[1], Dst->GetRowSize(avxsynth::PLANAR_U), Dst->GetHeight(avxsynth::PLANAR_U));
		Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_V), Dst->GetPitch(avxsynth::PLANAR_V), Frame->Data[2], Frame->Linesize[2], Dst->GetRowSize(avxsynth::PLANAR_V), Dst->GetHeight(avxsynth::PLANAR_V));
	} else if (VI.IsYUY2()) {
		Env->BitBlt(Dst->GetWritePtr(), Dst->GetPitch(), Frame->Data[0], Frame->Linesize[0], Dst->GetRowSize(), Dst->GetHeight());
	} else { // RGB
		Env->BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch(), Frame->Data[0], Frame->Linesize[0], Dst->GetRowSize(), Dst->GetHeight());
	}
}

void AvisynthVideoSource::OutputField(const FFMS_Frame *Frame, avxsynth::PVideoFrame &Dst, int Field, avxsynth::IScriptEnvironment *Env) {
	const FFMS_Frame *SrcPicture = (Frame);

	if (VI.pixel_type == avxsynth::VideoInfo::CS_I420) {
		if (Field) {
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_Y), Dst->GetPitch(avxsynth::PLANAR_Y) * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(avxsynth::PLANAR_Y), Dst->GetHeight(avxsynth::PLANAR_Y) / 2);
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_U), Dst->GetPitch(avxsynth::PLANAR_U) * 2, SrcPicture->Data[1], SrcPicture->Linesize[1] * 2, Dst->GetRowSize(avxsynth::PLANAR_U), Dst->GetHeight(avxsynth::PLANAR_U) / 2);
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_V), Dst->GetPitch(avxsynth::PLANAR_V) * 2, SrcPicture->Data[2], SrcPicture->Linesize[2] * 2, Dst->GetRowSize(avxsynth::PLANAR_V), Dst->GetHeight(avxsynth::PLANAR_V) / 2);
		} else {
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_Y) + Dst->GetPitch(avxsynth::PLANAR_Y), Dst->GetPitch(avxsynth::PLANAR_Y) * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(avxsynth::PLANAR_Y), Dst->GetHeight(avxsynth::PLANAR_Y) / 2);
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_U) + Dst->GetPitch(avxsynth::PLANAR_U), Dst->GetPitch(avxsynth::PLANAR_U) * 2, SrcPicture->Data[1] + SrcPicture->Linesize[1], SrcPicture->Linesize[1] * 2, Dst->GetRowSize(avxsynth::PLANAR_U), Dst->GetHeight(avxsynth::PLANAR_U) / 2);
			Env->BitBlt(Dst->GetWritePtr(avxsynth::PLANAR_V) + Dst->GetPitch(avxsynth::PLANAR_V), Dst->GetPitch(avxsynth::PLANAR_V) * 2, SrcPicture->Data[2] + SrcPicture->Linesize[2], SrcPicture->Linesize[2] * 2, Dst->GetRowSize(avxsynth::PLANAR_V), Dst->GetHeight(avxsynth::PLANAR_V) / 2);
		}
	} else if (VI.IsYUY2()) {
		if (Field)
			Env->BitBlt(Dst->GetWritePtr(), Dst->GetPitch() * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
		else
			Env->BitBlt(Dst->GetWritePtr() + Dst->GetPitch(), Dst->GetPitch() * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
	} else { // RGB
		if (Field)
			Env->BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch() * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
		else
			Env->BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 2), -Dst->GetPitch() * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
	}
}

avxsynth::PVideoFrame AvisynthVideoSource::GetFrame(int n, avxsynth::IScriptEnvironment *Env) {
	n = FFMIN(FFMAX(n,0), VI.num_frames - 1);

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	avxsynth::PVideoFrame Dst = Env->NewVideoFrame(VI);

	if (RFFMode > 0) {
		const FFMS_Frame *Frame = FFMS_GetFrame(V, FFMIN(FieldList[n].Top, FieldList[n].Bottom), &E);
		if (Frame == NULL)
			Env->ThrowError("FFVideoSource: %s", E.Buffer);
		if (FieldList[n].Top == FieldList[n].Bottom) {
			OutputFrame(Frame, Dst, Env);
		} else {
			int FirstField = FFMIN(FieldList[n].Top, FieldList[n].Bottom) == FieldList[n].Bottom;
			OutputField(Frame, Dst, FirstField, Env);
			Frame = FFMS_GetFrame(V, FFMAX(FieldList[n].Top, FieldList[n].Bottom), &E);
			if (Frame == NULL)
				Env->ThrowError("FFVideoSource: %s", E.Buffer);
			OutputField(Frame, Dst, !FirstField, Env);
		}
	} else {
		const FFMS_Frame *Frame;

		if (FPSNum > 0 && FPSDen > 0) {
			Frame = FFMS_GetFrameByTime(V, FFMS_GetVideoProperties(V)->FirstTime +
				(double)(n * (int64_t)FPSDen) / FPSNum, &E);
		} else {
			Frame = FFMS_GetFrame(V, n, &E);
			FFMS_Track *T = FFMS_GetTrackFromVideo(V);
			const FFMS_TrackTimeBase *TB = FFMS_GetTimeBase(T);
			Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFVFR_TIME"), static_cast<int>(FFMS_GetFrameInfo(T, n)->PTS * static_cast<double>(TB->Num) / TB->Den));
		}

		if (Frame == NULL)
			Env->ThrowError("FFVideoSource: %s", E.Buffer);

		Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFPICT_TYPE"), static_cast<int>(Frame->PictType));
		OutputFrame(Frame, Dst, Env);
	}

	return Dst;
}

bool AvisynthVideoSource::GetParity(int n) {
	return VI.image_type == avxsynth::VideoInfo::IT_TFF;
}

AvisynthAudioSource::AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index,
										 int AdjustDelay, const char *VarPrefix, avxsynth::IScriptEnvironment* Env) {
	memset(&VI, 0, sizeof(VI));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	A = FFMS_CreateAudioSource(SourceFile, Track, Index, AdjustDelay, &E);
	if (!A)
		Env->ThrowError("FFAudioSource: %s", E.Buffer);

	const FFMS_AudioProperties *AP = FFMS_GetAudioProperties(A);
	VI.nchannels = AP->Channels;

	VI.num_audio_samples = AP->NumSamples;
	VI.audio_samples_per_second = AP->SampleRate;

	// casting to int should be safe; none of the channel constants are greater than INT_MAX
	Env->SetVar(Env->Sprintf("%s%s", VarPrefix, "FFCHANNEL_LAYOUT"), static_cast<int>(AP->ChannelLayout));

	Env->SetGlobalVar("FFVAR_PREFIX", VarPrefix);

	switch (AP->SampleFormat) {
		case FFMS_FMT_U8: VI.sample_type  = avxsynth::SAMPLE_INT8; break;
		case FFMS_FMT_S16: VI.sample_type = avxsynth::SAMPLE_INT16; break;
		case FFMS_FMT_S32: VI.sample_type = avxsynth::SAMPLE_INT32; break;
		case FFMS_FMT_FLT: VI.sample_type = avxsynth::SAMPLE_FLOAT; break;
		default: Env->ThrowError("FFAudioSource: Bad audio format");
	}
}

AvisynthAudioSource::~AvisynthAudioSource() {
	FFMS_DestroyAudioSource(A);
}

void AvisynthAudioSource::GetAudio(void* Buf, avxsynth::__int64 Start, avxsynth::__int64 Count, avxsynth::IScriptEnvironment *Env) {
	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	if (FFMS_GetAudio(A, Buf, Start, Count, &E))
		Env->ThrowError("FFAudioSource: %s", E.Buffer);
}
