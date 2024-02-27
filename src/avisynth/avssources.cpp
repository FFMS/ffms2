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

#define NOMINMAX
#include "avssources.h"
#include "../vapoursynth/VSHelper4.h"
#include "../core/utils.h"

#include <algorithm>
#include <cassert>

static AVPixelFormat CSNameToPIXFMT(const char *CSName, AVPixelFormat Default, bool HighBitDepth) {
    if (!CSName)
        return AV_PIX_FMT_NONE;
    std::string s = CSName;
    std::transform(s.begin(), s.end(), s.begin(), toupper);
    if (s == "")
        return Default;
    if (s == "YUV9" || s == "YUV410P8")
        return AV_PIX_FMT_YUV410P;
    if (s == "YV411" || s == "YUV411P8")
        return AV_PIX_FMT_YUV411P;
    if (s == "YV12" || s == "YUV420P8")
        return AV_PIX_FMT_YUV420P;
    if (s == "YV16" || s == "YUV422P8")
        return AV_PIX_FMT_YUV422P;
    if (s == "YV24" || s == "YUV444P8")
        return AV_PIX_FMT_YUV444P;
    if (s == "Y8" || s == "GRAY8")
        return AV_PIX_FMT_GRAY8;
    if (s == "YUY2")
        return AV_PIX_FMT_YUYV422;
    if (s == "RGB24")
        return AV_PIX_FMT_BGR24;
    if (s == "RGB32")
        return AV_PIX_FMT_RGB32;
    if (HighBitDepth) {
        if (s == "YUVA420P8")
            return AV_PIX_FMT_YUVA420P;
        if (s == "YUVA422P8")
            return AV_PIX_FMT_YUVA422P;
        if (s == "YUVA444P8")
            return AV_PIX_FMT_YUVA444P;
        if (s == "YUV420P16")
            return AV_PIX_FMT_YUV420P16;
        if (s == "YUVA420P16")
            return AV_PIX_FMT_YUVA420P16;
        if (s == "YUV422P16")
            return AV_PIX_FMT_YUV422P16;
        if (s == "YUVA422P16")
            return AV_PIX_FMT_YUVA422P16;
        if (s == "YUV444P16")
            return AV_PIX_FMT_YUV444P16;
        if (s == "YUVA444P16")
            return AV_PIX_FMT_YUVA444P16;
        if (s == "YUV420P10")
            return AV_PIX_FMT_YUV420P10;
        if (s == "YUVA420P10")
            return AV_PIX_FMT_YUVA420P10;
        if (s == "YUV422P10")
            return AV_PIX_FMT_YUV422P10;
        if (s == "YUVA422P10")
            return AV_PIX_FMT_YUVA422P10;
        if (s == "YUV444P10")
            return AV_PIX_FMT_YUV444P10;
        if (s == "YUVA444P10")
            return AV_PIX_FMT_YUVA444P10;
        if (s == "RGBP8")
            return AV_PIX_FMT_GBRP;
        if (s == "RGBP10")
            return AV_PIX_FMT_GBRP10;
        if (s == "RGBP12")
            return AV_PIX_FMT_GBRP12;
        if (s == "RGBP16")
            return AV_PIX_FMT_GBRP16;
        if (s == "RGBPS")
            return AV_PIX_FMT_GBRPF32;
        if (s == "RGBAP8")
            return AV_PIX_FMT_GBRAP;
        if (s == "RGBAP10")
            return AV_PIX_FMT_GBRAP10;
        if (s == "RGBAP12")
            return AV_PIX_FMT_GBRAP12;
        if (s == "RGBAP16")
            return AV_PIX_FMT_GBRAP16;
        if (s == "RGBAPS")
            return AV_PIX_FMT_GBRAPF32;
        if (s == "Y10" || s == "GRAY10")
            return AV_PIX_FMT_GRAY10;
        if (s == "Y12" || s == "GRAY12")
            return AV_PIX_FMT_GRAY12;
        if (s == "Y16" || s == "GRAY16")
            return AV_PIX_FMT_GRAY16;
    }

    return AV_PIX_FMT_NONE;
}

AvisynthVideoSource::AvisynthVideoSource(const char *SourceFile, int Track, FFMS_Index *Index,
    int AFPSNum, int AFPSDen, int Threads, int SeekMode, int RFFMode,
    int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
    const char *ConvertToFormatName, const char *VarPrefix, IScriptEnvironment* Env)
    : FPSNum(AFPSNum)
    , FPSDen(AFPSDen)
    , RFFMode(RFFMode)
    , VarPrefix(VarPrefix) {
    VI = {};

    // check if the two functions we need for many bits are present
    VI.pixel_type = VideoInfo::CS_Y16;
    HighBitDepth = (VI.ComponentSize() == 2 && VI.IsY());
    VI.pixel_type = VideoInfo::CS_UNKNOWN;

    ErrorInfo E;
    V = FFMS_CreateVideoSource(SourceFile, Track, Index, Threads, SeekMode, &E);
    if (!V)
        Env->ThrowError("FFVideoSource: %s", E.Buffer);

    try {
        InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, ConvertToFormatName, Env);
    } catch (AvisynthError &) {
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

        int RepeatMin = FFMS_GetFrameInfo(VTrack, 0)->RepeatPict;
        int NumFields = 0;

        for (int i = 0; i < VP->NumFrames; i++) {
            int RepeatPict = FFMS_GetFrameInfo(VTrack, i)->RepeatPict;
            NumFields += RepeatPict + 1;
            RepeatMin = std::min(RepeatMin, RepeatPict);
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
            if (VP->TopFieldFirst) {
                for (auto &iter : FieldList)
                    std::swap(iter.Top, iter.Bottom);
            }

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
        VI.fps_denominator = VP->FPSDenominator;
        VI.fps_numerator = VP->FPSNumerator;
        VI.num_frames = VP->NumFrames;
        if (FPSNum > 0 && FPSDen > 0) {
            vsh::reduceRational(&FPSNum, &FPSDen);
            if (VI.fps_denominator != FPSDen || VI.fps_numerator != FPSNum) {
                VI.fps_denominator = FPSDen;
                VI.fps_numerator = FPSNum;
                if (VP->NumFrames > 1) {
                    VI.num_frames = static_cast<int>((VP->LastTime - VP->FirstTime) * (1 + 1. / (VP->NumFrames - 1)) * FPSNum / FPSDen + 0.5);
                    if (VI.num_frames < 1)
                        VI.num_frames = 1;
                } else {
                    VI.num_frames = 1;
                }
            } else {
                FPSNum = 0;
                FPSDen = 0;
            }
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

    has_at_least_v8 = true;
    try {
        Env->CheckVersion(8);
    } catch (const AvisynthError &) {
        has_at_least_v8 = false;
    }
}

AvisynthVideoSource::~AvisynthVideoSource() {
    FFMS_DestroyVideoSource(V);
}

static int GetSubSamplingH(const VideoInfo &vi) {
    if ((vi.IsYUV() || vi.IsYUVA()) && !vi.IsY() && vi.IsPlanar())
        return vi.GetPlaneHeightSubsampling(PLANAR_U);
    else
        return 0;
}

static int GetSubSamplingW(const VideoInfo &vi) {
    if ((vi.IsYUV() || vi.IsYUVA()) && !vi.IsY() && vi.IsPlanar())
        return vi.GetPlaneWidthSubsampling(PLANAR_U);
    else
        return 0;
}

void AvisynthVideoSource::InitOutputFormat(
    int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
    const char *ConvertToFormatName, IScriptEnvironment *Env) {

    ErrorInfo E;
    const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
    const FFMS_Frame *F = FFMS_GetFrame(V, 0, &E);
    if (!F)
        Env->ThrowError("FFVideoSource: %s", E.Buffer);

    std::vector<int> TargetFormats;
    if (HighBitDepth) {
        TargetFormats.push_back(FFMS_GetPixFmt("yuv420p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva420p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuv422p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva422p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuv444p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva444p16"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuv420p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva420p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuv422p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva422p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuv444p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva444p10"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrpf32"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrapf32"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrp16"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrap16"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrp12"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrap12"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrp10"));
        TargetFormats.push_back(FFMS_GetPixFmt("gbrap10"));
        TargetFormats.push_back(FFMS_GetPixFmt("gray16"));
        TargetFormats.push_back(FFMS_GetPixFmt("gray12"));
        TargetFormats.push_back(FFMS_GetPixFmt("gray10"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva420p"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva422p"));
        TargetFormats.push_back(FFMS_GetPixFmt("yuva444p"));
    }
    TargetFormats.push_back(FFMS_GetPixFmt("yuv410p"));
    TargetFormats.push_back(FFMS_GetPixFmt("yuv411p"));
    TargetFormats.push_back(FFMS_GetPixFmt("yuv420p"));
    TargetFormats.push_back(FFMS_GetPixFmt("yuv422p"));
    TargetFormats.push_back(FFMS_GetPixFmt("yuv444p"));
    TargetFormats.push_back(FFMS_GetPixFmt("gray8"));
    TargetFormats.push_back(FFMS_GetPixFmt("yuyv422"));
    TargetFormats.push_back(FFMS_GetPixFmt("bgra"));

    // Remove unsupported formats from list so they don't appear as an early termination
    TargetFormats.erase(std::remove(TargetFormats.begin(), TargetFormats.end(), -1), TargetFormats.end());

    TargetFormats.push_back(-1);

    // PIX_FMT_NV21 is misused as a return value different to the defined ones in the function
    AVPixelFormat TargetPixelFormat = CSNameToPIXFMT(ConvertToFormatName, AV_PIX_FMT_NV21, HighBitDepth);
    if (TargetPixelFormat == AV_PIX_FMT_NONE)
        Env->ThrowError("FFVideoSource: Invalid colorspace name specified");

    if (TargetPixelFormat != AV_PIX_FMT_NV21) {
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
        Env->ThrowError("FFVideoSource: Invalid resizer name specified");

    if (FFMS_SetOutputFormatV2(V, TargetFormats.data(),
        ResizeToWidth, ResizeToHeight, Resizer, &E))
        Env->ThrowError("FFVideoSource: No suitable output format found");

    F = FFMS_GetFrame(V, 0, &E);
    TargetFormats.clear();
    TargetFormats.push_back(F->ConvertedPixelFormat);
    TargetFormats.push_back(-1);

    // This trick is required to first get the "best" default format and then set only that format as the output
    if (FFMS_SetOutputFormatV2(V, TargetFormats.data(),
        ResizeToWidth, ResizeToHeight, Resizer, &E))
        Env->ThrowError("FFVideoSource: No suitable output format found");

    F = FFMS_GetFrame(V, 0, &E);

    if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuvj420p") || F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv420p"))
        VI.pixel_type = VideoInfo::CS_I420;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva420p"))
        VI.pixel_type = VideoInfo::CS_YUVA420;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuvj422p") || F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv422p"))
        VI.pixel_type = VideoInfo::CS_YV16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva422p"))
        VI.pixel_type = VideoInfo::CS_YUVA422;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuvj444p") || F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv444p"))
        VI.pixel_type = VideoInfo::CS_YV24;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva444p"))
        VI.pixel_type = VideoInfo::CS_YUVA444;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv411p"))
        VI.pixel_type = VideoInfo::CS_YV411;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv410p"))
        VI.pixel_type = VideoInfo::CS_YUV9;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gray8"))
        VI.pixel_type = VideoInfo::CS_Y8;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuyv422"))
        VI.pixel_type = VideoInfo::CS_YUY2;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("rgb32"))
        VI.pixel_type = VideoInfo::CS_BGR32;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("bgr24"))
        VI.pixel_type = VideoInfo::CS_BGR24;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv420p16"))
        VI.pixel_type = VideoInfo::CS_YUV420P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva420p16"))
        VI.pixel_type = VideoInfo::CS_YUVA420P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv422p16"))
        VI.pixel_type = VideoInfo::CS_YUV422P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva422p16"))
        VI.pixel_type = VideoInfo::CS_YUVA422P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv444p16"))
        VI.pixel_type = VideoInfo::CS_YUV444P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva444p16"))
        VI.pixel_type = VideoInfo::CS_YUVA444P16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv420p10"))
        VI.pixel_type = VideoInfo::CS_YUV420P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva420p10"))
        VI.pixel_type = VideoInfo::CS_YUVA420P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv422p10"))
        VI.pixel_type = VideoInfo::CS_YUV422P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva422p10"))
        VI.pixel_type = VideoInfo::CS_YUVA422P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuv444p10"))
        VI.pixel_type = VideoInfo::CS_YUV444P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("yuva444p10"))
        VI.pixel_type = VideoInfo::CS_YUVA444P10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrpf32"))
        VI.pixel_type = VideoInfo::CS_RGBPS;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrp16"))
        VI.pixel_type = VideoInfo::CS_RGBP16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrp12"))
        VI.pixel_type = VideoInfo::CS_RGBP12;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrp10"))
        VI.pixel_type = VideoInfo::CS_RGBP10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrapf32"))
        VI.pixel_type = VideoInfo::CS_RGBAPS;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrap16"))
        VI.pixel_type = VideoInfo::CS_RGBAP16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrap12"))
        VI.pixel_type = VideoInfo::CS_RGBAP12;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrap10"))
        VI.pixel_type = VideoInfo::CS_RGBAP10;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrp"))
        VI.pixel_type = VideoInfo::CS_RGBP;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gbrap"))
        VI.pixel_type = VideoInfo::CS_RGBAP;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gray16"))
        VI.pixel_type = VideoInfo::CS_Y16;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gray12"))
        VI.pixel_type = VideoInfo::CS_Y12;
    else if (F->ConvertedPixelFormat == FFMS_GetPixFmt("gray10"))
        VI.pixel_type = VideoInfo::CS_Y10;
    else
        Env->ThrowError("FFVideoSource: No suitable output format found");

    if (RFFMode > 0 && ResizeToHeight != F->EncodedHeight)
        Env->ThrowError("FFVideoSource: Vertical scaling not allowed in RFF mode");

    if (RFFMode > 0 && TargetPixelFormat != AV_PIX_FMT_NV21)
        Env->ThrowError("FFVideoSource: Only the default output colorspace can be used in RFF mode");

    // set color information variables
    Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCOLOR_SPACE"), F->ColorSpace);
    Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFCOLOR_RANGE"), F->ColorRange);

    if (VP->TopFieldFirst)
        VI.image_type = VideoInfo::IT_TFF;
    else
        VI.image_type = VideoInfo::IT_BFF;

    VI.width = F->ScaledWidth;
    VI.height = F->ScaledHeight;

    // Crop to obey subsampling width/height requirements
    VI.width -= VI.width % (1 << GetSubSamplingW(VI));
    VI.height -= VI.height % (1 << (GetSubSamplingH(VI) + (RFFMode > 0 ? 1 : 0)));
}

static void BlitPlane(const FFMS_Frame *Frame, PVideoFrame &Dst, IScriptEnvironment *Env, int Plane, int PlaneId) {
    Env->BitBlt(Dst->GetWritePtr(PlaneId), Dst->GetPitch(PlaneId),
        Frame->Data[Plane], Frame->Linesize[Plane],
        Dst->GetRowSize(PlaneId), Dst->GetHeight(PlaneId));
}

void AvisynthVideoSource::OutputFrame(const FFMS_Frame *Frame, PVideoFrame &Dst, IScriptEnvironment *Env) {
    if (VI.IsPlanar()) {
        BlitPlane(Frame, Dst, Env, 0, VI.IsRGB() ? PLANAR_G : PLANAR_Y);
        if (HighBitDepth ? !VI.IsY() : !VI.IsY8()) {
            BlitPlane(Frame, Dst, Env, 1, VI.IsRGB() ? PLANAR_B : PLANAR_U);
            BlitPlane(Frame, Dst, Env, 2, VI.IsRGB() ? PLANAR_R : PLANAR_V);
        }
        if (VI.IsYUVA() || VI.IsPlanarRGBA())
            BlitPlane(Frame, Dst, Env, 3, PLANAR_A);
    } else if (VI.IsYUY2()) {
        BlitPlane(Frame, Dst, Env, 0, 0);
    } else if (VI.IsRGB24() || VI.IsRGB32()) {
        Env->BitBlt(
            Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1), -Dst->GetPitch(),
            Frame->Data[0], Frame->Linesize[0],
            Dst->GetRowSize(), Dst->GetHeight());
    } else {
        assert(false);
    }
}

static void BlitField(const FFMS_Frame *Frame, PVideoFrame &Dst, IScriptEnvironment *Env, int Plane, int PlaneId, int Field) {
    Env->BitBlt(
        Dst->GetWritePtr(PlaneId) + Dst->GetPitch(PlaneId) * Field, Dst->GetPitch(PlaneId) * 2,
        Frame->Data[Plane] + Frame->Linesize[Plane] * Field, Frame->Linesize[Plane] * 2,
        Dst->GetRowSize(PlaneId), Dst->GetHeight(PlaneId) / 2);
}

void AvisynthVideoSource::OutputField(const FFMS_Frame *Frame, PVideoFrame &Dst, int Field, IScriptEnvironment *Env) {
    const FFMS_Frame *SrcPicture = Frame;
    if (VI.IsPlanar()) {
        BlitField(Frame, Dst, Env, 0, VI.IsRGB() ? PLANAR_G : PLANAR_Y, Field);
        if (HighBitDepth ? !VI.IsY() : !VI.IsY8()) {
            BlitField(Frame, Dst, Env, 1, VI.IsRGB() ? PLANAR_B : PLANAR_U, Field);
            BlitField(Frame, Dst, Env, 2, VI.IsRGB() ? PLANAR_R : PLANAR_V, Field);
        }
        if (VI.IsYUVA() || VI.IsPlanarRGBA())
            BlitField(Frame, Dst, Env, 3, PLANAR_A, Field);
    } else if (VI.IsYUY2()) {
        BlitField(Frame, Dst, Env, 0, 0, Field);
    } else if (VI.IsRGB24() || VI.IsRGB32()) {
        Env->BitBlt(
            Dst->GetWritePtr() + Dst->GetPitch() * (Dst->GetHeight() - 1 - Field), -Dst->GetPitch() * 2,
            SrcPicture->Data[0] + SrcPicture->Linesize[0] * Field, SrcPicture->Linesize[0] * 2,
            Dst->GetRowSize(), Dst->GetHeight() / 2);
    } else {
        assert(false);
    }
}

PVideoFrame AvisynthVideoSource::GetFrame(int n, IScriptEnvironment *Env) {
    n = std::min(std::max(n, 0), VI.num_frames - 1);

    PVideoFrame Dst = Env->NewVideoFrame(VI);
    AVSMap *props = has_at_least_v8 ? Env->getFramePropsRW(Dst) : nullptr;

    ErrorInfo E;
    const FFMS_Frame *Frame;
    if (RFFMode > 0) {
        Frame = FFMS_GetFrame(V, std::min(FieldList[n].Top, FieldList[n].Bottom), &E);
        if (Frame == nullptr)
            Env->ThrowError("FFVideoSource: %s", E.Buffer);
        if (FieldList[n].Top == FieldList[n].Bottom) {
            OutputFrame(Frame, Dst, Env);
        } else {
            int FirstField = std::min(FieldList[n].Top, FieldList[n].Bottom) == (FFMS_GetVideoProperties(V)->TopFieldFirst ? FieldList[n].Top : FieldList[n].Bottom);
            OutputField(Frame, Dst, FirstField, Env);
            Frame = FFMS_GetFrame(V, std::max(FieldList[n].Top, FieldList[n].Bottom), &E);
            if (Frame == nullptr)
                Env->ThrowError("FFVideoSource: %s", E.Buffer);
            OutputField(Frame, Dst, !FirstField, Env);
        }
        Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFVFR_TIME"), -1);
        Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFPICT_TYPE"), static_cast<int>('U'));
        if (has_at_least_v8) {
            Env->propSetInt(props, "_DurationNum", FPSNum, 0);
            Env->propSetInt(props, "_DurationDen", FPSDen, 0);
            // don't set absolute time since it's ill-defined
        }
    } else {
        if (FPSNum > 0 && FPSDen > 0) {
            double currentTime = FFMS_GetVideoProperties(V)->FirstTime +
                (double)(n * (int64_t)FPSDen) / FPSNum;
            Frame = FFMS_GetFrameByTime(V, currentTime, &E);
            Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFVFR_TIME"), -1);
            if (has_at_least_v8) {
                Env->propSetInt(props, "_DurationNum", FPSDen, 0);
                Env->propSetInt(props, "_DurationDen", FPSNum, 0);
                Env->propSetFloat(props, "_AbsoluteTime", currentTime, 0);
            }
        } else {
            Frame = FFMS_GetFrame(V, n, &E);
            FFMS_Track *T = FFMS_GetTrackFromVideo(V);
            const FFMS_TrackTimeBase *TB = FFMS_GetTimeBase(T);
            Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFVFR_TIME"), static_cast<int>(FFMS_GetFrameInfo(T, n)->PTS * static_cast<double>(TB->Num) / TB->Den));
            if (has_at_least_v8) {
                int64_t num;
                if (n + 1 < VI.num_frames)
                    num = FFMS_GetFrameInfo(T, n + 1)->PTS - FFMS_GetFrameInfo(T, n)->PTS;
                else if (n > 0) // simply use the second to last frame's duration for the last one, should be good enough
                    num = FFMS_GetFrameInfo(T, n)->PTS - FFMS_GetFrameInfo(T, n - 1)->PTS;
                else // just make it one timebase if it's a single frame clip
                    num = 1;
                int64_t DurNum = TB->Num * num;
                int64_t DurDen = TB->Den * 1000;
                vsh::reduceRational(&DurNum, &DurDen);
                Env->propSetInt(props, "_DurationNum", DurNum, 0);
                Env->propSetInt(props, "_DurationDen", DurDen, 0);
                Env->propSetFloat(props, "_AbsoluteTime", ((static_cast<double>(TB->Num) / 1000) * FFMS_GetFrameInfo(T, n)->PTS) / TB->Den, 0);
            }
        }

        if (Frame == nullptr)
            Env->ThrowError("FFVideoSource: %s", E.Buffer);

        Env->SetVar(Env->Sprintf("%s%s", this->VarPrefix, "FFPICT_TYPE"), static_cast<int>(Frame->PictType));
        OutputFrame(Frame, Dst, Env);
    }

    if (has_at_least_v8) {
        const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
        if (VP->SARNum > 0 && VP->SARDen > 0) {
            Env->propSetInt(props, "_SARNum", VP->SARNum, 0);
            Env->propSetInt(props, "_SARDen", VP->SARDen, 0);
        }

        Env->propSetInt(props, "_Matrix", Frame->ColorSpace, 0);
        Env->propSetInt(props, "_Primaries", Frame->ColorPrimaries, 0);
        Env->propSetInt(props, "_Transfer", Frame->TransferCharateristics, 0);
        if (Frame->ChromaLocation > 0)
            Env->propSetInt(props, "_ChromaLocation", Frame->ChromaLocation - 1, 0);

        if (Frame->ColorRange == FFMS_CR_MPEG)
            Env->propSetInt(props, "_ColorRange", 1, 0);
        else if (Frame->ColorRange == FFMS_CR_JPEG)
            Env->propSetInt(props, "_ColorRange", 0, 0);
        if (RFFMode == 0)
            Env->propSetData(props, "_PictType", &Frame->PictType, 1, 0);

        // Set field information
        int FieldBased = 0;
        if (Frame->InterlacedFrame)
            FieldBased = (Frame->TopFieldFirst ? 2 : 1);
        Env->propSetInt(props, "_FieldBased", FieldBased, 0);

        if (Frame->HasMasteringDisplayPrimaries) {
            Env->propSetFloatArray(props, "MasteringDisplayPrimariesX", Frame->MasteringDisplayPrimariesX, 3);
            Env->propSetFloatArray(props, "MasteringDisplayPrimariesY", Frame->MasteringDisplayPrimariesY, 3);
            Env->propSetFloat(props, "MasteringDisplayWhitePointX", Frame->MasteringDisplayWhitePointX, 0);
            Env->propSetFloat(props, "MasteringDisplayWhitePointY", Frame->MasteringDisplayWhitePointY, 0);
        }

        if (Frame->HasMasteringDisplayLuminance) {
            Env->propSetFloat(props, "MasteringDisplayMinLuminance", Frame->MasteringDisplayMinLuminance, 0);
            Env->propSetFloat(props, "MasteringDisplayMaxLuminance", Frame->MasteringDisplayMaxLuminance, 0);
        }

        if (Frame->HasContentLightLevel) {
            Env->propSetFloat(props, "ContentLightLevelMax", Frame->ContentLightLevelMax, 0);
            Env->propSetFloat(props, "ContentLightLevelAverage", Frame->ContentLightLevelAverage, 0);
        }

        if (Frame->DolbyVisionRPU && Frame->DolbyVisionRPUSize > 0) {
            Env->propSetData(props, "DolbyVisionRPU", reinterpret_cast<const char *>(Frame->DolbyVisionRPU), Frame->DolbyVisionRPUSize, 0);
        }

        if (Frame->HDR10Plus && Frame->HDR10PlusSize > 0) {
            Env->propSetData(props, "HDR10Plus", reinterpret_cast<const char *>(Frame->HDR10Plus), Frame->HDR10PlusSize, 0);
        }
    }

    return Dst;
}

bool AvisynthVideoSource::GetParity(int n) {
    return VI.image_type == VideoInfo::IT_TFF;
}

AvisynthAudioSource::AvisynthAudioSource(const char *SourceFile, int Track, FFMS_Index *Index,
    int AdjustDelay, int FillGaps, double DrcScale, const char *VarPrefix, IScriptEnvironment* Env) {
    VI = {};

    ErrorInfo E;
    A = FFMS_CreateAudioSource2(SourceFile, Track, Index, AdjustDelay, FillGaps, DrcScale, &E);
    if (!A)
        Env->ThrowError("FFAudioSource: %s", E.Buffer);

    const FFMS_AudioProperties *AP = FFMS_GetAudioProperties(A);
    VI.nchannels = AP->Channels;
    VI.num_audio_samples = AP->NumSamples;
    VI.audio_samples_per_second = AP->SampleRate;
    VI.SetChannelMask(true, AP->ChannelLayout);

    Env->SetVar(Env->Sprintf("%s%s", VarPrefix, "FFCHANNEL_LAYOUT"), static_cast<int>(AP->ChannelLayout));

    Env->SetGlobalVar("FFVAR_PREFIX", VarPrefix);

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

void AvisynthAudioSource::GetAudio(void* Buf, int64_t Start, int64_t Count, IScriptEnvironment *Env) {
    ErrorInfo E;
    if (FFMS_GetAudio(A, Buf, Start, Count, &E))
        Env->ThrowError("FFAudioSource: %s", E.Buffer);
}
