//  Copyright (c) 2012-2018 Fredrik Mellbin
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

#include "vapoursource4.h"
#include "VSHelper4.h"
#include "../core/utils.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static int GetNumPixFmts() {
    int n = 0;
    while (av_get_pix_fmt_name((AVPixelFormat)n))
        n++;
    return n;
}

static bool IsRealNativeEndianPlanar(const AVPixFmtDescriptor &desc) {
    // reject all special flags
    if (desc.flags & (AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BAYER | AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM))
        return false;
    int used_planes = 0;
    for (int i = 0; i < desc.nb_components; i++)
        used_planes = std::max(used_planes, (int)desc.comp[i].plane + 1);
    bool temp = (used_planes == desc.nb_components) && (desc.comp[0].depth >= 8);
    if (!temp)
        return false; 
    else if (desc.comp[0].depth == 8)
        return temp;
    else
        return (AV_PIX_FMT_YUV420P10 == AV_PIX_FMT_YUV420P10BE ? !!(desc.flags & AV_PIX_FMT_FLAG_BE) : !(desc.flags & AV_PIX_FMT_FLAG_BE));
}

static int GetSampleType(const AVPixFmtDescriptor &desc) {
    return (desc.flags & AV_PIX_FMT_FLAG_FLOAT) ? stFloat : stInteger;
}

static bool HasAlpha(const AVPixFmtDescriptor &desc) {
    return !!(desc.flags & AV_PIX_FMT_FLAG_ALPHA);
}

static int GetColorFamily(const AVPixFmtDescriptor &desc) {
    if (desc.nb_components <= 2)
        return cfGray;
    else if (desc.flags & AV_PIX_FMT_FLAG_RGB)
        return cfRGB;
    else
        return cfYUV;
}

static int FormatConversionToPixelFormat(int id, bool alpha, VSCore *core, const VSAPI *vsapi) {
    VSVideoFormat f;
    vsapi->getVideoFormatByID(&f, id, core);
    int npixfmt = GetNumPixFmts();
    // Look for a suitable format without alpha first to not waste memory
    if (!alpha) {
        for (int i = 0; i < npixfmt; i++) {
            const AVPixFmtDescriptor &desc = *av_pix_fmt_desc_get((AVPixelFormat)i);
            if (IsRealNativeEndianPlanar(desc) && !HasAlpha(desc)
                && GetColorFamily(desc) == f.colorFamily
                && desc.comp[0].depth == f.bitsPerSample
                && desc.log2_chroma_w == f.subSamplingW
                && desc.log2_chroma_h == f.subSamplingH
                && GetSampleType(desc) == f.sampleType)
                return i;
        }
    }
    // Try all remaining formats
    for (int i = 0; i < npixfmt; i++) {
        const AVPixFmtDescriptor &desc = *av_pix_fmt_desc_get((AVPixelFormat)i);
        if (IsRealNativeEndianPlanar(desc) && HasAlpha(desc)
            && GetColorFamily(desc) == f.colorFamily
            && desc.comp[0].depth == f.bitsPerSample
            && desc.log2_chroma_w == f.subSamplingW
            && desc.log2_chroma_h == f.subSamplingH
            && GetSampleType(desc) == f.sampleType)
            return i;
    }
    return AV_PIX_FMT_NONE;
}

static void FormatConversionToVS(VSVideoFormat &f, int id, VSCore *core, const VSAPI *vsapi) {
    const AVPixFmtDescriptor &desc = *av_pix_fmt_desc_get((AVPixelFormat)id);
    vsapi->queryVideoFormat(&f,
        GetColorFamily(desc),
        GetSampleType(desc),
        desc.comp[0].depth,
        desc.log2_chroma_w,
        desc.log2_chroma_h, core);
}

const VSFrame *VS_CC VSVideoSource4::GetVSFrame(int n, VSCore *core, const VSAPI *vsapi) {
    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);

    VSFrame *Dst = vsapi->newVideoFrame(&VI[0].format, VI[0].width, VI[0].height, nullptr, core);
    VSMap *Props = vsapi->getFramePropertiesRW(Dst);

    const FFMS_Frame *Frame = nullptr;

    if (FPSNum > 0 && FPSDen > 0) {
        double currentTime = FFMS_GetVideoProperties(V)->FirstTime +
            (double)(n * (int64_t)FPSDen) / FPSNum;
        Frame = FFMS_GetFrameByTime(V, currentTime, &E);
        vsapi->mapSetInt(Props, "_DurationNum", FPSDen, maReplace);
        vsapi->mapSetInt(Props, "_DurationDen", FPSNum, maReplace);
        vsapi->mapSetFloat(Props, "_AbsoluteTime", currentTime, maReplace);
    } else {
        Frame = FFMS_GetFrame(V, n, &E);
        FFMS_Track *T = FFMS_GetTrackFromVideo(V);
        const FFMS_TrackTimeBase *TB = FFMS_GetTimeBase(T);
        int64_t num;
        if (n + 1 < VI[0].numFrames)
            num = FFMS_GetFrameInfo(T, n + 1)->PTS - FFMS_GetFrameInfo(T, n)->PTS;
        else if (n > 0) // simply use the second to last frame's duration for the last one, should be good enough
            num = FFMS_GetFrameInfo(T, n)->PTS - FFMS_GetFrameInfo(T, n - 1)->PTS;
        else // just make it one timebase if it's a single frame clip
            num = 1;
        int64_t DurNum = TB->Num * num;
        int64_t DurDen = TB->Den * 1000;
        vsh::reduceRational(&DurNum, &DurDen);
        vsapi->mapSetInt(Props, "_DurationNum", DurNum, maReplace);
        vsapi->mapSetInt(Props, "_DurationDen", DurDen, maReplace);
        vsapi->mapSetFloat(Props, "_AbsoluteTime", ((static_cast<double>(TB->Num) / 1000) * FFMS_GetFrameInfo(T, n)->PTS) / TB->Den, maReplace);
    }

    if (Frame == nullptr)
        throw std::runtime_error(E.Buffer);

    // Set AR variables
    if (SARNum > 0 && SARDen > 0) {
        vsapi->mapSetInt(Props, "_SARNum", SARNum, maReplace);
        vsapi->mapSetInt(Props, "_SARDen", SARDen, maReplace);
    }

    vsapi->mapSetInt(Props, "_Matrix", Frame->ColorSpace, maReplace);
    vsapi->mapSetInt(Props, "_Primaries", Frame->ColorPrimaries, maReplace);
    vsapi->mapSetInt(Props, "_Transfer", Frame->TransferCharateristics, maReplace);
    if (Frame->ChromaLocation > 0)
        vsapi->mapSetInt(Props, "_ChromaLocation", Frame->ChromaLocation - 1, maReplace);

    if (Frame->ColorRange == FFMS_CR_MPEG)
        vsapi->mapSetInt(Props, "_ColorRange", 1, maReplace);
    else if (Frame->ColorRange == FFMS_CR_JPEG)
        vsapi->mapSetInt(Props, "_ColorRange", 0, maReplace);
    vsapi->mapSetData(Props, "_PictType", &Frame->PictType, 1, dtUtf8, maReplace);

    // Set field information
    int FieldBased = 0;
    if (Frame->InterlacedFrame)
        FieldBased = (Frame->TopFieldFirst ? 2 : 1);
    vsapi->mapSetInt(Props, "_FieldBased", FieldBased, maReplace);

    if (Frame->HasMasteringDisplayPrimaries) {
        vsapi->mapSetFloatArray(Props, "MasteringDisplayPrimariesX", Frame->MasteringDisplayPrimariesX, 3);
        vsapi->mapSetFloatArray(Props, "MasteringDisplayPrimariesY", Frame->MasteringDisplayPrimariesY, 3);
        vsapi->mapSetFloat(Props, "MasteringDisplayWhitePointX", Frame->MasteringDisplayWhitePointX, maReplace);
        vsapi->mapSetFloat(Props, "MasteringDisplayWhitePointY", Frame->MasteringDisplayWhitePointY, maReplace);
    }

    if (Frame->HasMasteringDisplayLuminance) {
        vsapi->mapSetFloat(Props, "MasteringDisplayMinLuminance", Frame->MasteringDisplayMinLuminance, maReplace);
        vsapi->mapSetFloat(Props, "MasteringDisplayMaxLuminance", Frame->MasteringDisplayMaxLuminance, maReplace);
    }

    if (Frame->HasContentLightLevel) {
        vsapi->mapSetFloat(Props, "ContentLightLevelMax", Frame->ContentLightLevelMax, maReplace);
        vsapi->mapSetFloat(Props, "ContentLightLevelAverage", Frame->ContentLightLevelAverage, maReplace);
    }

    if (Frame->DolbyVisionRPU && Frame->DolbyVisionRPUSize > 0) {
        vsapi->mapSetData(Props, "DolbyVisionRPU", reinterpret_cast<const char *>(Frame->DolbyVisionRPU), Frame->DolbyVisionRPUSize, dtBinary, maReplace);
    }

    if (Frame->HDR10Plus && Frame->HDR10PlusSize > 0) {
        vsapi->mapSetData(Props, "HDR10Plus", reinterpret_cast<const char *>(Frame->HDR10Plus), Frame->HDR10PlusSize, dtBinary, maReplace);
    }

    OutputFrame(Frame, Dst, vsapi);
    if (OutputAlpha) {
        VSFrame *AlphaDst = vsapi->newVideoFrame(&VI[1].format, VI[1].width, VI[1].height, nullptr, core);
        vsapi->mapSetInt(vsapi->getFramePropertiesRW(AlphaDst), "_ColorRange", 0, maReplace);
        OutputAlphaFrame(Frame, VI[0].format.numPlanes, AlphaDst, vsapi);
        vsapi->mapConsumeFrame(Props, "_Alpha", AlphaDst, maReplace);
    }

    const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);
    vsapi->mapSetInt(Props, "Flip", VP->Flip, maReplace);
    vsapi->mapSetInt(Props, "Rotation", VP->Rotation, maReplace);

    return Dst;
}

const VSFrame *VS_CC VSVideoSource4::GetFrame(int n, int activationReason, void *instanceData, void **, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    VSVideoSource4 *vs = static_cast<VSVideoSource4 *>(instanceData);
    if (activationReason == arInitial) {
        if (vs->LastFrame < n && vs->LastFrame > n - vs->CacheThreshold) {
            for (int i = vs->LastFrame + 1; i < n; i++) {
                try {
                    const VSFrame *frame = vs->GetVSFrame(i, core, vsapi);
                    vsapi->cacheFrame(frame, i, frameCtx);
                    vsapi->freeFrame(frame);
                } catch (std::runtime_error &) {
                    // don't care if it errors out on frames we don't actually have to deliver
                }
            }
        }

        try {
            const VSFrame *frame = vs->GetVSFrame(n, core, vsapi);
            vs->LastFrame = n;
            return frame;
        } catch (std::runtime_error &e) {
            vsapi->setFilterError(e.what(), frameCtx);
        }

    }

    return nullptr;
}

void VS_CC VSVideoSource4::Free(void *instanceData, VSCore *, const VSAPI *) {
    FFMS_Deinit();
    delete static_cast<VSVideoSource4 *>(instanceData);
}

VSVideoSource4::VSVideoSource4(const char *SourceFile, int Track, FFMS_Index *Index,
    int AFPSNum, int AFPSDen, int Threads, int SeekMode, int /*RFFMode*/,
    int ResizeToWidth, int ResizeToHeight, const char *ResizerName,
    int Format, bool OutputAlpha, const VSAPI *vsapi, VSCore *core)
    : FPSNum(AFPSNum), FPSDen(AFPSDen), OutputAlpha(OutputAlpha) {

    VI[0] = {};
    VI[1] = {};

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);

    V = FFMS_CreateVideoSource(SourceFile, Track, Index, Threads, SeekMode, &E);
    if (!V) {
        throw std::runtime_error(std::string("Source: ") + E.Buffer);
    }
    try {
        InitOutputFormat(ResizeToWidth, ResizeToHeight, ResizerName, Format, vsapi, core);
    } catch (std::exception &) {
        FFMS_DestroyVideoSource(V);
        throw;
    }

    const FFMS_VideoProperties *VP = FFMS_GetVideoProperties(V);

    VI[0].fpsDen = VP->FPSDenominator;
    VI[0].fpsNum = VP->FPSNumerator;
    VI[0].numFrames = VP->NumFrames;
    vsh::reduceRational(&VI[0].fpsNum, &VI[0].fpsDen);

    if (FPSNum > 0 && FPSDen > 0) {
        vsh::reduceRational(&FPSNum, &FPSDen);
        if (VI[0].fpsDen != FPSDen || VI[0].fpsNum != FPSNum) {
            VI[0].fpsDen = FPSDen;
            VI[0].fpsNum = FPSNum;
            if (VP->NumFrames > 1) {
                VI[0].numFrames = static_cast<int>((VP->LastTime - VP->FirstTime) * (1 + 1. / (VP->NumFrames - 1)) * FPSNum / FPSDen + 0.5);
                if (VI[0].numFrames < 1)
                    VI[0].numFrames = 1;
            } else {
                VI[0].numFrames = 1;
            }
        } else {
            FPSNum = 0;
            FPSDen = 0;
        }
    }

    if (OutputAlpha) {
        VI[1] = VI[0];
        vsapi->queryVideoFormat(&VI[1].format, cfGray, VI[0].format.sampleType, VI[0].format.bitsPerSample, 0, 0, core);
    }

    SARNum = VP->SARNum;
    SARDen = VP->SARDen;
}

VSVideoSource4::~VSVideoSource4() {
    FFMS_DestroyVideoSource(V);
}

const VSVideoInfo *VSVideoSource4::GetVideoInfo() const {
    return &VI[0];
}

void VSVideoSource4::SetCacheThreshold(int threshold) {
    CacheThreshold = threshold;
}

void VSVideoSource4::InitOutputFormat(int ResizeToWidth, int ResizeToHeight,
    const char *ResizerName, int ConvertToFormat, const VSAPI *vsapi, VSCore *core) {

    char ErrorMsg[1024];
    FFMS_ErrorInfo E;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);

    const FFMS_Frame *F = FFMS_GetFrame(V, 0, &E);
    if (!F) {
        std::string buf = "Source: ";
        buf += E.Buffer;
        throw std::runtime_error(buf);
    }

    std::vector<int> TargetFormats;
    int npixfmt = GetNumPixFmts();
    for (int i = 0; i < npixfmt; i++)
        if (IsRealNativeEndianPlanar(*av_pix_fmt_desc_get((AVPixelFormat)i)))
            TargetFormats.push_back(i);
    TargetFormats.push_back(AV_PIX_FMT_NONE);

    int TargetPixelFormat = AV_PIX_FMT_NONE;
    if (ConvertToFormat != pfNone) {
        TargetPixelFormat = FormatConversionToPixelFormat(ConvertToFormat, OutputAlpha, core, vsapi);
        if (TargetPixelFormat == AV_PIX_FMT_NONE)
            throw std::runtime_error(std::string("Source: Invalid output colorspace specified"));

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
        throw std::runtime_error(std::string("Source: Invalid resizer name specified"));

    if (FFMS_SetOutputFormatV2(V, &TargetFormats[0],
        ResizeToWidth, ResizeToHeight, Resizer, &E))
        throw std::runtime_error(std::string("Source: No suitable output format found"));

    F = FFMS_GetFrame(V, 0, &E);
    TargetFormats.clear();
    TargetFormats.push_back(F->ConvertedPixelFormat);
    TargetFormats.push_back(-1);

    // This trick is required to first get the "best" default format and then set only that format as the output
    if (FFMS_SetOutputFormatV2(V, TargetFormats.data(), ResizeToWidth, ResizeToHeight, Resizer, &E))
        throw std::runtime_error(std::string("Source: No suitable output format found"));

    F = FFMS_GetFrame(V, 0, &E);

    // Don't output alpha if the clip doesn't have it
    if (!HasAlpha(*av_pix_fmt_desc_get((AVPixelFormat)F->ConvertedPixelFormat)))
        OutputAlpha = false;

     FormatConversionToVS(VI[0].format, F->ConvertedPixelFormat, core, vsapi);
    if (VI[0].format.colorFamily == cfUndefined)
        throw std::runtime_error(std::string("Source: No suitable output format found"));

    VI[0].width = F->ScaledWidth;
    VI[0].height = F->ScaledHeight;

    // Crop to obey subsampling width/height requirements
    VI[0].width -= VI[0].width % (1 << VI[0].format.subSamplingW);
    VI[0].height -= VI[0].height % (1 << VI[0].format.subSamplingH);
}

void VSVideoSource4::OutputFrame(const FFMS_Frame *Frame, VSFrame *Dst, const VSAPI *vsapi) {
    const int RGBPlaneOrder[3] = { 2, 0, 1 };
    const VSVideoFormat *fi = vsapi->getVideoFrameFormat(Dst);
    if (fi->colorFamily == cfRGB) {
        for (int i = 0; i < fi->numPlanes; i++)
            vsh::bitblt(vsapi->getWritePtr(Dst, i), vsapi->getStride(Dst, i), Frame->Data[RGBPlaneOrder[i]], Frame->Linesize[RGBPlaneOrder[i]],
                vsapi->getFrameWidth(Dst, i) * fi->bytesPerSample, vsapi->getFrameHeight(Dst, i));
    } else {
        for (int i = 0; i < fi->numPlanes; i++)
            vsh::bitblt(vsapi->getWritePtr(Dst, i), vsapi->getStride(Dst, i), Frame->Data[i], Frame->Linesize[i],
                vsapi->getFrameWidth(Dst, i) * fi->bytesPerSample, vsapi->getFrameHeight(Dst, i));
    }
}

void VSVideoSource4::OutputAlphaFrame(const FFMS_Frame *Frame, int Plane, VSFrame *Dst, const VSAPI *vsapi) {
    const VSVideoFormat *fi = vsapi->getVideoFrameFormat(Dst);
    vsh::bitblt(vsapi->getWritePtr(Dst, 0), vsapi->getStride(Dst, 0), Frame->Data[Plane], Frame->Linesize[Plane],
        vsapi->getFrameWidth(Dst, 0) * fi->bytesPerSample, vsapi->getFrameHeight(Dst, 0));
}
