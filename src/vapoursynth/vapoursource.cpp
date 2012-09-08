//  Copyright (c) 2007-2012 Fredrik Mellbin
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

#include "vapoursource.h"
#include "../avisynth/avsutils.h"
#include <libavutil/common.h>
#include <libavcodec/avcodec.h>
#include <cmath>
#include <utility>
#include <vector>

typedef std::pair<int, int> IntPair;

void BitBlt(uint8_t* dstp, int dst_pitch, const uint8_t* srcp, int src_pitch, int row_size, int height) {
    if (src_pitch == dst_pitch && dst_pitch == row_size) {
        memcpy(dstp, srcp, row_size * height);
    } else {
        for (int i = 0; i < height; i++) {
            memcpy(dstp, srcp, row_size);
            dstp += dst_pitch;
            srcp += src_pitch;
        }
    }
}

const IntPair ip[] = {
	IntPair(pfGray8, PIX_FMT_GRAY8),
	IntPair(pfGray16, PIX_FMT_GRAY16),
	IntPair(pfYUV420P8, PIX_FMT_YUV420P),
	IntPair(pfYUV422P8, PIX_FMT_YUV422P),
	IntPair(pfYUV444P8, PIX_FMT_YUV444P),
	IntPair(pfYUV410P8, PIX_FMT_YUV410P),
	IntPair(pfYUV411P8, PIX_FMT_YUV411P),
	IntPair(pfYUV440P8, PIX_FMT_YUV440P),
	IntPair(pfYUV420P9, PIX_FMT_YUV420P9),
	IntPair(pfYUV422P9, PIX_FMT_YUV422P9),
	IntPair(pfYUV444P9, PIX_FMT_YUV444P9),
	IntPair(pfYUV420P10, PIX_FMT_YUV420P10),
	IntPair(pfYUV422P10, PIX_FMT_YUV422P10),
	IntPair(pfYUV444P10, PIX_FMT_YUV444P10),
	IntPair(pfYUV420P16, PIX_FMT_YUV420P16),
	IntPair(pfYUV422P16, PIX_FMT_YUV420P16),
	IntPair(pfYUV444P16, PIX_FMT_YUV444P16),
	IntPair(pfRGB24, PIX_FMT_GBRP),
	IntPair(pfRGB27, PIX_FMT_GBRP9),
	IntPair(pfRGB30, PIX_FMT_GBRP10),
	IntPair(pfRGB48, PIX_FMT_GBRP16),
	IntPair(pfCompatBgr32, PIX_FMT_RGB32),
	IntPair(pfCompayYuy2, PIX_FMT_YUYV422),
	// shitty compat bullshit crap
	IntPair(pfYUV420P8, PIX_FMT_YUVJ420P),
    IntPair(pfYUV422P8, PIX_FMT_YUVJ422P),
    IntPair(pfYUV444P8, PIX_FMT_YUVJ444P),
    IntPair(pfYUV440P8, PIX_FMT_YUVJ440P)
};

static int formatConversion(int id, bool toPixelFormat, VSCore *core, const VSAPI *vsapi) {
	// fixme, maybe this should use FFMS_GetPixFmt()
	for (int i = 0; i < sizeof(ip)/sizeof(ip[0]); i++) {
		if (toPixelFormat) {
			if (ip[i].first == id)
				return ip[i].second;
		} else {
			if (ip[i].second == id)
				return ip[i].first;
		}
	}

	if (toPixelFormat)
		return PIX_FMT_NONE;
	else
		return pfNone;
}

void __stdcall VSVideoSource::Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)*instanceData;
	vsapi->setVideoInfo(&vs->VI, node);
}

const VSFrameRef *__stdcall VSVideoSource::GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)*instanceData;
	if (activationReason == arInitial) {
		if (vs->VI.numFrames && n >= vs->VI.numFrames)
			n = vs->VI.numFrames - 1;

		char ErrorMsg[1024];
		FFMS_ErrorInfo E;
		E.Buffer = ErrorMsg;
		E.BufferSize = sizeof(ErrorMsg);
		std::string buf = "Source: ";

		VSFrameRef *Dst = vsapi->newVideoFrame(vs->VI.format, vs->VI.width, vs->VI.height, NULL, core);
		VSMap *Props = vsapi->getFramePropsRW(Dst);

		// fixme, set some frame properties in rffmode if anyone complains
		if (vs->RFFMode > 0) {
			const FFMS_Frame *Frame = FFMS_GetFrame(vs->V, FFMIN(vs->FieldList[n].Top, vs->FieldList[n].Bottom), &E);
			if (Frame == NULL) {
				buf += E.Buffer;
				vsapi->setFilterError(buf.c_str(), frameCtx);
				return NULL;
			}
			if (vs->FieldList[n].Top == vs->FieldList[n].Bottom) {
				OutputFrame(Frame, Dst, vsapi, core);
			} else {
				int FirstField = FFMIN(vs->FieldList[n].Top, vs->FieldList[n].Bottom) == vs->FieldList[n].Bottom;
				OutputField(Frame, Dst, FirstField, vsapi, core);
				Frame = FFMS_GetFrame(vs->V, FFMAX(vs->FieldList[n].Top, vs->FieldList[n].Bottom), &E);
				if (Frame == NULL) {
					buf += E.Buffer;
					vsapi->setFilterError(buf.c_str(), frameCtx);
					return NULL;
				}
				OutputField(Frame, Dst, !FirstField, vsapi, core);
			}
		} else {
			const FFMS_Frame *Frame;

			if (vs->FPSNum > 0 && vs->FPSDen > 0) {
				Frame = FFMS_GetFrameByTime(vs->V, FFMS_GetVideoProperties(vs->V)->FirstTime +
					(double)(n * (int64_t)vs->FPSDen) / vs->FPSNum, &E);
				vsapi->propSetInt(Props, "_DurationNum", vs->FPSDen, 0);
				vsapi->propSetInt(Props, "_DurationDen", vs->FPSNum, 0);
			} else {
				Frame = FFMS_GetFrame(vs->V, n, &E);
				FFMS_Track *T = FFMS_GetTrackFromVideo(vs->V);
				const FFMS_TrackTimeBase *TB = FFMS_GetTimeBase(T);
				int64_t num;
				if (n + 1 < vs->VI.numFrames)
					num = FFMS_GetFrameInfo(T, n + 1)->PTS - FFMS_GetFrameInfo(T, n)->PTS;
				else // simply use the second to last frame's duration for the last one, should be good enough
					num = FFMS_GetFrameInfo(T, n)->PTS - FFMS_GetFrameInfo(T, n - 1)->PTS;
				vsapi->propSetInt(Props, "_DurationNum", TB->Num * num, 0);
				vsapi->propSetInt(Props, "_DurationDen", TB->Den, 0);
			}

			if (Frame == NULL) {
				buf += E.Buffer;
				vsapi->setFilterError(buf.c_str(), frameCtx);
				return NULL;
			}

			// Set AR variables
			if (vs->SARNum > 0 && vs->SARDen > 0) {
				vsapi->propSetInt(Props, "_SARNum", vs->SARNum, 0);
				vsapi->propSetInt(Props, "_SARDen", vs->SARDen, 0);
			}

			vsapi->propSetInt(Props, "_ColorSpace", Frame->ColorSpace, 0);
			vsapi->propSetInt(Props, "_ColorRange", Frame->ColorRange, 0);
			vsapi->propSetData(Props, "_PictType", &Frame->PictType, 1, 0);

			OutputFrame(Frame, Dst, vsapi, core);
		}

		return Dst;
	}

	return NULL;
}

void __stdcall VSVideoSource::Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)instanceData;
	delete vs;
}

VSVideoSource::VSVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
		int FPSNum, int FPSDen, int Threads, int SeekMode, int RFFMode,
		int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
		int Format, const VSAPI *vsapi, VSCore *core)
		: FPSNum(FPSNum), FPSDen(FPSDen), RFFMode(RFFMode) {

	memset(&VI, 0, sizeof(VI));

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	V = FFMS_CreateVideoSource(SourceFile, Track, Index, Threads, SeekMode, &E);
	if (!V) {
		std::string buf = "Source: ";
		buf += E.Buffer;
		throw std::exception(buf.c_str());
	}
	try {
		InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, Format, vsapi, core);
	} catch (std::exception &e) {
		FFMS_DestroyVideoSource(V);
		throw;
	}

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

	if (RFFMode > 0) {
		// This part assumes things, and so should you

		FFMS_Track *VTrack = FFMS_GetTrackFromVideo(V);

		if (FFMS_GetFrameInfo(VTrack, 0)->RepeatPict < 0) {
			FFMS_DestroyVideoSource(V);
			throw std::exception("Source: No RFF flags present");
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
			throw std::exception("Source: Unsupported RFF flag pattern");
			}
		}

		VI.fpsDen = VP->RFFDenominator * (RepeatMin + 1);
		VI.fpsNum = VP->RFFNumerator;
		VI.numFrames = (NumFields + RepeatMin) / (RepeatMin + 1);

		int DestField = 0;
		FieldList.resize(VI.numFrames);
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
			VI.numFrames = (VI.numFrames * 4) / 5;
			VI.fpsDen *= 5;
			VI.fpsNum *= 4;

			int OutputFrames = 0;

			for (int i = 0; i < VI.numFrames / 4; i++) {
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

			FieldList.resize(VI.numFrames);
		}
	} else {
		if (FPSNum > 0 && FPSDen > 0) {
			VI.fpsDen = FPSDen;
			VI.fpsNum = FPSNum;
			if (VP->NumFrames > 1) {
				VI.numFrames = static_cast<int>((VP->LastTime - VP->FirstTime) * (1 + 1. / (VP->NumFrames - 1)) * FPSNum / FPSDen + 0.5);
				if (VI.numFrames < 1) VI.numFrames = 1;
			} else {
				VI.numFrames = 1;
			}
		} else {
			VI.fpsDen = VP->FPSDenominator;
			VI.fpsNum = VP->FPSNumerator;
			VI.numFrames = VP->NumFrames;
		}
	}

	SARNum = VP->SARNum;
	SARDen = VP->SARDen;
}

void __stdcall Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)*instanceData;
	vsapi->setVideoInfo(vs->GetVI(), node);
}

void __stdcall Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	VSVideoSource *vs = (VSVideoSource *)instanceData;
	delete vs;
}

VSVideoSource::~VSVideoSource() {
	FFMS_DestroyVideoSource(V);
}

void VSVideoSource::InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
		const char *ResizerName, int ConvertToFormat, const VSAPI *vsapi, VSCore *core) {

	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
	const FFMS_Frame *F = FFMS_GetFrame(V, 0, &E);
	if (!F) {
		std::string buf = "Source: ";
		buf += E.Buffer;
		throw std::exception(buf.c_str());
	}

	std::vector<int> TargetFormats;
	for (int i = 0; i < sizeof(ip)/sizeof(ip[0]); i++)
		TargetFormats.push_back(ip[i].second);
	TargetFormats.push_back(-1);

	int TargetPixelFormat = PIX_FMT_NONE;
	if (ConvertToFormat != pfNone) {
		TargetPixelFormat = formatConversion(ConvertToFormat, true, core, vsapi);
		if (TargetPixelFormat == PIX_FMT_NONE)
			throw std::exception("Source: Invalid output colorspace specified");

		TargetFormats.clear();
		TargetFormats.push_back(TargetPixelFormat);
		TargetFormats.push_back(-1);
	}

	if (ResizeToWidth <= 0)
		ResizeToWidth = F->EncodedWidth;

	if (ResizeToHeight <= 0)
		ResizeToHeight = F->EncodedHeight;

	int Resizer = ResizerNameToSWSResizer(ResizerName);
	if (Resizer == 0)
		throw std::exception("Source: Invalid resizer name specified");

	if (FFMS_SetOutputFormatV2(V, &TargetFormats[0],
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		throw std::exception("Source: No suitable output format found");

	F = FFMS_GetFrame(V, 0, &E);
	TargetFormats.clear();
	TargetFormats.push_back(F->ConvertedPixelFormat);
	TargetFormats.push_back(-1);

	// This trick is required to first get the "best" default format and then set only that format as the output
	if (FFMS_SetOutputFormatV2(V, &TargetFormats[0],
		ResizeToWidth, ResizeToHeight, Resizer, &E))
		throw std::exception("Source: No suitable output format found");

	F = FFMS_GetFrame(V, 0, &E);

	VI.format = vsapi->getFormatPreset(formatConversion(F->ConvertedPixelFormat, false, core, vsapi), core);
	if (!VI.format)
		throw std::exception("Source: No suitable output format found");

	if (RFFMode > 0 && ResizeToHeight != F->EncodedHeight)
		throw std::exception("Source: Vertical scaling not allowed in RFF mode");

	if (RFFMode > 0 && 	TargetPixelFormat != pfNone)
		throw std::exception("Source: Only the default output colorspace can be used in RFF mode");

	VI.width = F->ScaledWidth;
	VI.height = F->ScaledHeight;

	// fixme? Crop to obey sane even width/height requirements

	// fixme, should be an error condition
	if (RFFMode > 0) {
		VI.height -= VI.height & 1;
	}
}

void VSVideoSource::OutputFrame(const FFMS_Frame *Frame, VSFrameRef *Dst, const VSAPI *vsapi, VSCore *core) {
	const VSFormat *fi = vsapi->getFrameFormat(Dst);
	for (int i = 0; i < fi->numPlanes; i++) {
		BitBlt(vsapi->getWritePtr(Dst, i), vsapi->getStride(Dst, i), Frame->Data[0], Frame->Linesize[0], vsapi->getFrameWidth(Dst, i) * fi->bytesPerSample, vsapi->getFrameHeight(Dst, i));
	}

	// fixme, flip packed rgb?
	//BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch(), Frame->Data[0], Frame->Linesize[0], Dst->GetRowSize(), Dst->GetHeight());
}

void VSVideoSource::OutputField(const FFMS_Frame *Frame, VSFrameRef *Dst, int Field, const VSAPI *vsapi, VSCore *core) {
	const FFMS_Frame *SrcPicture = (Frame);
	/*
	if (VI.pixel_type == VideoInfo::CS_I420) {
		if (Field) {
			BitBlt(Dst->GetWritePtr(PLANAR_Y), Dst->GetPitch(PLANAR_Y) * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(PLANAR_Y), Dst->GetHeight(PLANAR_Y) / 2);
			BitBlt(Dst->GetWritePtr(PLANAR_U), Dst->GetPitch(PLANAR_U) * 2, SrcPicture->Data[1], SrcPicture->Linesize[1] * 2, Dst->GetRowSize(PLANAR_U), Dst->GetHeight(PLANAR_U) / 2);
			BitBlt(Dst->GetWritePtr(PLANAR_V), Dst->GetPitch(PLANAR_V) * 2, SrcPicture->Data[2], SrcPicture->Linesize[2] * 2, Dst->GetRowSize(PLANAR_V), Dst->GetHeight(PLANAR_V) / 2);
		} else {
			BitBlt(Dst->GetWritePtr(PLANAR_Y) + Dst->GetPitch(PLANAR_Y), Dst->GetPitch(PLANAR_Y) * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(PLANAR_Y), Dst->GetHeight(PLANAR_Y) / 2);
			BitBlt(Dst->GetWritePtr(PLANAR_U) + Dst->GetPitch(PLANAR_U), Dst->GetPitch(PLANAR_U) * 2, SrcPicture->Data[1] + SrcPicture->Linesize[1], SrcPicture->Linesize[1] * 2, Dst->GetRowSize(PLANAR_U), Dst->GetHeight(PLANAR_U) / 2);
			BitBlt(Dst->GetWritePtr(PLANAR_V) + Dst->GetPitch(PLANAR_V), Dst->GetPitch(PLANAR_V) * 2, SrcPicture->Data[2] + SrcPicture->Linesize[2], SrcPicture->Linesize[2] * 2, Dst->GetRowSize(PLANAR_V), Dst->GetHeight(PLANAR_V) / 2);
		}
	} else if (VI.IsYUY2()) {
		if (Field)
			BitBlt(Dst->GetWritePtr(), Dst->GetPitch() * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
		else
			BitBlt(Dst->GetWritePtr() + Dst->GetPitch(), Dst->GetPitch() * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
	} else { // RGB
		if (Field)
			BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch() * 2, SrcPicture->Data[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
		else
			BitBlt(Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 2), -Dst->GetPitch() * 2, SrcPicture->Data[0] + SrcPicture->Linesize[0], SrcPicture->Linesize[0] * 2, Dst->GetRowSize(), Dst->GetHeight() / 2);
	}
	*/
}
