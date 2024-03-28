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

#include "videosource.h"
#include "indexing.h"
#include "videoutils.h"
#include <algorithm>
#include <thread>


void FFMS_VideoSource::SanityCheckFrameForData(AVFrame *Frame) {
    for (int i = 0; i < 4; i++) {
        if (Frame->data[i] != nullptr && Frame->linesize[i] != 0)
            return;
    }

    throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC, "Insanity detected: decoder returned an empty frame");
}

void FFMS_VideoSource::GetFrameCheck(int n) {
    if (n < 0 || n >= VP.NumFrames)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
            "Out of bounds frame requested");
}

FFMS_Frame *FFMS_VideoSource::OutputFrame(AVFrame *Frame) {
    SanityCheckFrameForData(Frame);

    if (LastFrameWidth != Frame->width || LastFrameHeight != Frame->height || LastFramePixelFormat != Frame->format) {
        if (TargetHeight > 0 && TargetWidth > 0 && !TargetPixelFormats.empty()) {
            if (!InputFormatOverridden) {
                InputFormat = AV_PIX_FMT_NONE;
                InputColorSpace = AVCOL_SPC_UNSPECIFIED;
                InputColorRange = AVCOL_RANGE_UNSPECIFIED;
            }

            ReAdjustOutputFormat(Frame);
        } else {
            OutputFormat = (AVPixelFormat) Frame->format;
        }
    }

    if (SWS) {
        sws_scale(SWS, Frame->data, Frame->linesize, 0, Frame->height, SWSFrameData, SWSFrameLinesize);
        for (int i = 0; i < 4; i++) {
            LocalFrame.Data[i] = SWSFrameData[i];
            LocalFrame.Linesize[i] = SWSFrameLinesize[i];
        }
    } else {
        // Special case to avoid ugly casts
        for (int i = 0; i < 4; i++) {
            LocalFrame.Data[i] = Frame->data[i];
            LocalFrame.Linesize[i] = Frame->linesize[i];
        }
    }

    LocalFrame.EncodedWidth = Frame->width;
    LocalFrame.EncodedHeight = Frame->height;
    LocalFrame.EncodedPixelFormat = Frame->format;
    LocalFrame.ScaledWidth = TargetWidth;
    LocalFrame.ScaledHeight = TargetHeight;
    LocalFrame.ConvertedPixelFormat = OutputFormat;
    LocalFrame.KeyFrame = !!(Frame->flags & AV_FRAME_FLAG_KEY);
    LocalFrame.PictType = av_get_picture_type_char(Frame->pict_type);
    LocalFrame.RepeatPict = Frame->repeat_pict;
    LocalFrame.InterlacedFrame = !!(Frame->flags & AV_FRAME_FLAG_INTERLACED);
    LocalFrame.TopFieldFirst = !!(Frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
    LocalFrame.ColorSpace = OutputColorSpaceSet ? OutputColorSpace : Frame->colorspace;
    LocalFrame.ColorRange = OutputColorRangeSet ? OutputColorRange : Frame->color_range;
    LocalFrame.ColorPrimaries = (OutputColorPrimaries >= 0) ? OutputColorPrimaries : Frame->color_primaries;
    LocalFrame.TransferCharateristics = (OutputTransferCharateristics >= 0) ? OutputTransferCharateristics : Frame->color_trc;
    LocalFrame.ChromaLocation = (OutputChromaLocation >= 0) ? OutputChromaLocation : Frame->chroma_location;

    const AVFrameSideData *MasteringDisplaySideData = av_frame_get_side_data(Frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (MasteringDisplaySideData) {
        const AVMasteringDisplayMetadata *MasteringDisplay = reinterpret_cast<const AVMasteringDisplayMetadata *>(MasteringDisplaySideData->data);
        if (MasteringDisplay->has_primaries) {
            LocalFrame.HasMasteringDisplayPrimaries = MasteringDisplay->has_primaries;
            for (int i = 0; i < 3; i++) {
                LocalFrame.MasteringDisplayPrimariesX[i] = av_q2d(MasteringDisplay->display_primaries[i][0]);
                LocalFrame.MasteringDisplayPrimariesY[i] = av_q2d(MasteringDisplay->display_primaries[i][1]);
            }
            LocalFrame.MasteringDisplayWhitePointX = av_q2d(MasteringDisplay->white_point[0]);
            LocalFrame.MasteringDisplayWhitePointY = av_q2d(MasteringDisplay->white_point[1]);
        }
        if (MasteringDisplay->has_luminance) {
            LocalFrame.HasMasteringDisplayLuminance = MasteringDisplay->has_luminance;
            LocalFrame.MasteringDisplayMinLuminance = av_q2d(MasteringDisplay->min_luminance);
            LocalFrame.MasteringDisplayMaxLuminance = av_q2d(MasteringDisplay->max_luminance);
        }
    }
    LocalFrame.HasMasteringDisplayPrimaries = !!LocalFrame.MasteringDisplayPrimariesX[0] && !!LocalFrame.MasteringDisplayPrimariesY[0] &&
                                              !!LocalFrame.MasteringDisplayPrimariesX[1] && !!LocalFrame.MasteringDisplayPrimariesY[1] &&
                                              !!LocalFrame.MasteringDisplayPrimariesX[2] && !!LocalFrame.MasteringDisplayPrimariesY[2] &&
                                              !!LocalFrame.MasteringDisplayWhitePointX   && !!LocalFrame.MasteringDisplayWhitePointY;
    /* MasteringDisplayMinLuminance can be 0 */
    LocalFrame.HasMasteringDisplayLuminance = !!LocalFrame.MasteringDisplayMaxLuminance;

    const AVFrameSideData *DolbyVisionRPUSideData = av_frame_get_side_data(Frame, AV_FRAME_DATA_DOVI_RPU_BUFFER);
    if (DolbyVisionRPUSideData) {
        if (DolbyVisionRPUSideData->size > RPUBufferSize) {
            void *tmp = av_realloc(RPUBuffer, DolbyVisionRPUSideData->size);
            if (!tmp)
                throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                                     "Could not allocate RPU buffer.");
            RPUBuffer = reinterpret_cast<uint8_t *>(tmp);
            RPUBufferSize = DolbyVisionRPUSideData->size;
        }

        memcpy(RPUBuffer, DolbyVisionRPUSideData->data, DolbyVisionRPUSideData->size);

        LocalFrame.DolbyVisionRPU = RPUBuffer;
        LocalFrame.DolbyVisionRPUSize = DolbyVisionRPUSideData->size;
    }

    AVFrameSideData *HDR10PlusSideData = av_frame_get_side_data(Frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
    if (HDR10PlusSideData) {
        uint8_t *T35Buffer = nullptr;
        size_t T35Size;
        int ret = av_dynamic_hdr_plus_to_t35(reinterpret_cast<const AVDynamicHDRPlus *>(HDR10PlusSideData->data), &T35Buffer, &T35Size);
        if (ret < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
                                     "HDR10+ dynamic metadata could not be serialized.");
        if (T35Size > HDR10PlusBufferSize) {
            void *tmp = av_realloc(HDR10PlusBuffer, T35Size);
            if (!tmp)
                throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                                     "Could not allocate HDR10+ buffer.");
            HDR10PlusBuffer = reinterpret_cast<uint8_t *>(tmp);
            HDR10PlusBufferSize = T35Size;
        }

        memcpy(HDR10PlusBuffer, T35Buffer, T35Size);
        av_free(T35Buffer);

        LocalFrame.HDR10Plus = HDR10PlusBuffer;
        LocalFrame.HDR10PlusSize = T35Size;
    }

    const AVFrameSideData *ContentLightSideData = av_frame_get_side_data(Frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (ContentLightSideData) {
        const AVContentLightMetadata *ContentLightLevel = reinterpret_cast<const AVContentLightMetadata *>(ContentLightSideData->data);
        LocalFrame.ContentLightLevelMax = ContentLightLevel->MaxCLL;
        LocalFrame.ContentLightLevelAverage = ContentLightLevel->MaxFALL;
    }
    /* Only check for either of them */
    LocalFrame.HasContentLightLevel = !!LocalFrame.ContentLightLevelMax || !!LocalFrame.ContentLightLevelAverage;

    LastFrameHeight = Frame->height;
    LastFrameWidth = Frame->width;
    LastFramePixelFormat = (AVPixelFormat) Frame->format;

    return &LocalFrame;
}

FFMS_VideoSource::FFMS_VideoSource(const char *SourceFile, FFMS_Index &Index, int Track, int Threads, int SeekMode)
    : Index(Index), SeekMode(SeekMode) {

    try {
        if (Track < 0 || Track >= static_cast<int>(Index.size()))
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Out of bounds track index selected");

        if (Index[Track].TT != FFMS_TYPE_VIDEO)
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Not a video track");

        if (Index[Track].empty())
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Video track contains no frames");

        if (!Index.CompareFileSignature(SourceFile))
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
                "The index does not match the source file");

        Frames = Index[Track];
        VideoTrack = Track;

        if (Threads < 1)
            DecodingThreads = (std::min)(std::thread::hardware_concurrency(), 16u);
        else
            DecodingThreads = Threads;

        DecodeFrame = av_frame_alloc();
        LastDecodedFrame = av_frame_alloc();
        StashedPacket = av_packet_alloc();

        if (!DecodeFrame || !LastDecodedFrame || !StashedPacket)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate dummy frame / stashed packet.");

        // Dummy allocations so the unallocated case doesn't have to be handled later
        if (av_image_alloc(SWSFrameData, SWSFrameLinesize, 16, 16, AV_PIX_FMT_GRAY8, 4) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate dummy frame.");

        LAVFOpenFile(SourceFile, FormatContext, VideoTrack, Index.LAVFOpts);

        auto *Codec = avcodec_find_decoder(FormatContext->streams[VideoTrack]->codecpar->codec_id);
        if (Codec == nullptr)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Video codec not found");

        CodecContext = avcodec_alloc_context3(Codec);
        if (CodecContext == nullptr)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate video codec context.");
        if (avcodec_parameters_to_context(CodecContext, FormatContext->streams[VideoTrack]->codecpar) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Could not copy video decoder parameters.");
        CodecContext->thread_count = DecodingThreads;
        CodecContext->has_b_frames = Frames.MaxBFrames;

        // Full explanation by more clever person availale here: https://github.com/Nevcairiel/LAVFilters/issues/113
        if (CodecContext->codec_id == AV_CODEC_ID_H264 && CodecContext->has_b_frames)
            CodecContext->has_b_frames = 15; // the maximum possible value for h264

        if (avcodec_open2(CodecContext, Codec, nullptr) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Could not open video codec");

        // Similar yet different to h264 workaround above
        // vc1 simply sets has_b_frames to 1 no matter how many there are so instead we set it to the max value
        // in order to not confuse our own delay guesses later
        // Doesn't affect actual vc1 reordering unlike h264
        if (CodecContext->codec_id == AV_CODEC_ID_VC1 && CodecContext->has_b_frames) {
            Delay = 7 + (CodecContext->thread_count - 1); // the maximum possible value for vc1
        } else if (CodecContext->codec_id == AV_CODEC_ID_AV1) {
            // libdav1d.c exports delay like this.
            Delay = CodecContext->delay;
        } else {
            // In theory we can move this to CodecContext->delay, sort of, one day, maybe. Not now.
            Delay = CodecContext->has_b_frames; // Normal decoder delay
            if (CodecContext->active_thread_type & FF_THREAD_FRAME) // Adjust for frame based threading
                Delay += CodecContext->thread_count - 1;
        }

        // Always try to decode a frame to make sure all required parameters are known
        int64_t DummyPTS = 0, DummyPos = 0;
        DecodeNextFrame(DummyPTS, DummyPos);

        //VP.image_type = VideoInfo::IT_TFF;
        VP.FPSDenominator = FormatContext->streams[VideoTrack]->time_base.num;
        VP.FPSNumerator = FormatContext->streams[VideoTrack]->time_base.den;

        // sanity check framerate
        if (VP.FPSDenominator <= 0 || VP.FPSNumerator <= 0) {
            VP.FPSDenominator = 1;
            VP.FPSNumerator = 30;
        }

        // Calculate the average framerate
        size_t TotalFrames = 0;
        for (size_t i = 0; i < Frames.size(); i++)
            if (!Frames[i].Hidden)
                TotalFrames++;

        if (TotalFrames >= 2) {
            double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
            double TD = (double)(Frames.TB.Den);
            double TN = (double)(Frames.TB.Num);
            VP.FPSDenominator = (unsigned int)(PTSDiff * TN / TD * 1000.0 / (TotalFrames - 1));
            VP.FPSNumerator = 1000000;
        } else if (TotalFrames == 1 && Frames.LastDuration > 0) {
            VP.FPSDenominator *= Frames.LastDuration;
        }

        // Set the video properties from the codec context
        SetVideoProperties();

        // Set the SAR from the container if the codec SAR is invalid
        if (VP.SARNum <= 0 || VP.SARDen <= 0) {
            VP.SARNum = FormatContext->streams[VideoTrack]->sample_aspect_ratio.num;
            VP.SARDen = FormatContext->streams[VideoTrack]->sample_aspect_ratio.den;
        }

        // Set stereoscopic 3d type
        VP.Stereo3DType = FFMS_S3D_TYPE_2D;
        VP.Stereo3DFlags = 0;

        for (int i = 0; i < FormatContext->streams[VideoTrack]->codecpar->nb_coded_side_data; i++) {
            if (FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].type == AV_PKT_DATA_STEREO3D) {
                const AVStereo3D *StereoSideData = (const AVStereo3D *)FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].data;
                VP.Stereo3DType = StereoSideData->type;
                VP.Stereo3DFlags = StereoSideData->flags;
            } else if (FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA) {
                const AVMasteringDisplayMetadata *MasteringDisplay = (const AVMasteringDisplayMetadata *)FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].data;
                if (MasteringDisplay->has_primaries) {
                    VP.HasMasteringDisplayPrimaries = MasteringDisplay->has_primaries;
                    for (int i = 0; i < 3; i++) {
                        VP.MasteringDisplayPrimariesX[i] = av_q2d(MasteringDisplay->display_primaries[i][0]);
                        VP.MasteringDisplayPrimariesY[i] = av_q2d(MasteringDisplay->display_primaries[i][1]);
                    }
                    VP.MasteringDisplayWhitePointX = av_q2d(MasteringDisplay->white_point[0]);
                    VP.MasteringDisplayWhitePointY = av_q2d(MasteringDisplay->white_point[1]);
                }
                if (MasteringDisplay->has_luminance) {
                    VP.HasMasteringDisplayLuminance = MasteringDisplay->has_luminance;
                    VP.MasteringDisplayMinLuminance = av_q2d(MasteringDisplay->min_luminance);
                    VP.MasteringDisplayMaxLuminance = av_q2d(MasteringDisplay->max_luminance);
                }

                VP.HasMasteringDisplayPrimaries = !!VP.MasteringDisplayPrimariesX[0] && !!VP.MasteringDisplayPrimariesY[0] &&
                                                  !!VP.MasteringDisplayPrimariesX[1] && !!VP.MasteringDisplayPrimariesY[1] &&
                                                  !!VP.MasteringDisplayPrimariesX[2] && !!VP.MasteringDisplayPrimariesY[2] &&
                                                  !!VP.MasteringDisplayWhitePointX   && !!VP.MasteringDisplayWhitePointY;
                /* MasteringDisplayMinLuminance can be 0 */
                VP.HasMasteringDisplayLuminance = !!VP.MasteringDisplayMaxLuminance;
            } else if (FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL) {
                const AVContentLightMetadata *ContentLightLevel = (const AVContentLightMetadata *)FormatContext->streams[VideoTrack]->codecpar->coded_side_data[i].data;

                VP.ContentLightLevelMax = ContentLightLevel->MaxCLL;
                VP.ContentLightLevelAverage = ContentLightLevel->MaxFALL;

                /* Only check for either of them */
                VP.HasContentLightLevel = !!VP.ContentLightLevelMax || !!VP.ContentLightLevelAverage;
            }
        }

        // Set rotation
        VP.Rotation = 0;
        VP.Flip = 0;
        const AVPacketSideData *SideData = av_packet_side_data_get(FormatContext->streams[VideoTrack]->codecpar->coded_side_data, FormatContext->streams[VideoTrack]->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        const int32_t *RotationMatrixSrc = reinterpret_cast<const int32_t *>(SideData ? SideData->data : nullptr);
        if (RotationMatrixSrc) {
            int32_t RotationMatrix[9];
            memcpy(RotationMatrix, RotationMatrixSrc, sizeof(RotationMatrix));
            int64_t det = (int64_t)RotationMatrix[0] * RotationMatrix[4] - (int64_t)RotationMatrix[1] * RotationMatrix[3];
            if (det < 0) {
                /* Always assume an horizontal flip for simplicity, it can be changed later if rotation is 180. */
                VP.Flip = 1;

                /* Flip the matrix to decouple flip and rotation operations. */
                av_display_matrix_flip(RotationMatrix, 1, 0);
            }

            int rot = lround(av_display_rotation_get(RotationMatrix));

            if (rot == 180 && det < 0) {
                /* This is a vertical flip with no rotation. */
                VP.Flip = -1;
            } else {
                /* It is possible to have a 90/270 rotation and a horizontal flip:
                 * in this case, the rotation angle applies to the video frame
                 * (rather than the rendering frame), so add this step to nullify
                 * the conversion below. */
                if (VP.Flip)
                    rot *= -1;

                /* Return a positive value, noting that this converts angles
                 * from the rendering frame to the video frame. */
                VP.Rotation = -rot;
                if (VP.Rotation < 0)
                    VP.Rotation += 360;
            }
        }

        if (SeekMode >= 0 && Frames.size() > 1) {
            if (Seek(0) < 0) {
                throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                    "Video track is unseekable");
            }
        }

        // Cannot "output" without doing all other initialization
        // This is the additional mess required for seekmode=-1 to work in a reasonable way
        OutputFrame(DecodeFrame);

        if (LocalFrame.HasMasteringDisplayPrimaries) {
            VP.HasMasteringDisplayPrimaries = LocalFrame.HasMasteringDisplayPrimaries;
            for (int i = 0; i < 3; i++) {
                VP.MasteringDisplayPrimariesX[i] = LocalFrame.MasteringDisplayPrimariesX[i];
                VP.MasteringDisplayPrimariesY[i] = LocalFrame.MasteringDisplayPrimariesY[i];
            }

            // Simply copy this from the first frame to make it easier to access
            VP.MasteringDisplayWhitePointX = LocalFrame.MasteringDisplayWhitePointX;
            VP.MasteringDisplayWhitePointY = LocalFrame.MasteringDisplayWhitePointY;
        }
        if (LocalFrame.HasMasteringDisplayLuminance) {
            VP.HasMasteringDisplayLuminance = LocalFrame.HasMasteringDisplayLuminance;
            VP.MasteringDisplayMinLuminance = LocalFrame.MasteringDisplayMinLuminance;
            VP.MasteringDisplayMaxLuminance = LocalFrame.MasteringDisplayMaxLuminance;
        }
        if (LocalFrame.HasContentLightLevel) {
            VP.HasContentLightLevel = LocalFrame.HasContentLightLevel;
            VP.ContentLightLevelMax = LocalFrame.ContentLightLevelMax;
            VP.ContentLightLevelAverage = LocalFrame.ContentLightLevelAverage;
        }
    } catch (FFMS_Exception &) {
        Free();
        throw;
    }
}

FFMS_VideoSource::~FFMS_VideoSource() {
    Free();
}

FFMS_Frame *FFMS_VideoSource::GetFrameByTime(double Time) {
    // The final 1/1000th of a PTS is added to avoid frame duplication due to floating point math inexactness
    // Basically only a problem when the fps is externally set to the same or a multiple of the input clip fps
    int Frame = Frames.ClosestFrameFromPTS(static_cast<int64_t>(((Time * 1000 * Frames.TB.Den) / Frames.TB.Num) + .001));
    return GetFrame(Frame);
}

static AVColorRange handle_jpeg(AVPixelFormat *format) {
    switch (*format) {
    case AV_PIX_FMT_YUVJ420P: *format = AV_PIX_FMT_YUV420P; return AVCOL_RANGE_JPEG;
    case AV_PIX_FMT_YUVJ422P: *format = AV_PIX_FMT_YUV422P; return AVCOL_RANGE_JPEG;
    case AV_PIX_FMT_YUVJ444P: *format = AV_PIX_FMT_YUV444P; return AVCOL_RANGE_JPEG;
    case AV_PIX_FMT_YUVJ440P: *format = AV_PIX_FMT_YUV440P; return AVCOL_RANGE_JPEG;
    default:                                                      return AVCOL_RANGE_UNSPECIFIED;
    }
}

void FFMS_VideoSource::SetOutputFormat(const AVPixelFormat *TargetFormats, int Width, int Height, int Resizer) {
    TargetWidth = Width;
    TargetHeight = Height;
    TargetResizer = Resizer;
    TargetPixelFormats.clear();
    while (*TargetFormats != AV_PIX_FMT_NONE)
        TargetPixelFormats.push_back(*TargetFormats++);
    OutputColorSpaceSet = true;
    OutputColorRangeSet = true;
    OutputFormat = AV_PIX_FMT_NONE;

    ReAdjustOutputFormat(DecodeFrame);
    OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetInputFormat(int ColorSpace, int ColorRange, AVPixelFormat Format) {
    InputFormatOverridden = true;

    if (Format != AV_PIX_FMT_NONE)
        InputFormat = Format;
    if (ColorRange != AVCOL_RANGE_UNSPECIFIED)
        InputColorRange = (AVColorRange)ColorRange;
    if (ColorSpace != AVCOL_SPC_UNSPECIFIED)
        InputColorSpace = (AVColorSpace)ColorSpace;

    if (TargetPixelFormats.size()) {
        ReAdjustOutputFormat(DecodeFrame);
        OutputFrame(DecodeFrame);
    }
}

void FFMS_VideoSource::DetectInputFormat() {
    if (InputFormat == AV_PIX_FMT_NONE)
        InputFormat = CodecContext->pix_fmt;

    AVColorRange RangeFromFormat = handle_jpeg(&InputFormat);

    if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
        InputColorRange = RangeFromFormat;
    if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
        InputColorRange = CodecContext->color_range;

    if (InputColorSpace == AVCOL_SPC_UNSPECIFIED)
        InputColorSpace = CodecContext->colorspace;
}

void FFMS_VideoSource::ReAdjustOutputFormat(AVFrame *Frame) {
    if (SWS) {
        sws_freeContext(SWS);
        SWS = nullptr;
    }

    DetectInputFormat();

    OutputFormat = FindBestPixelFormat(TargetPixelFormats, InputFormat);
    if (OutputFormat == AV_PIX_FMT_NONE) {
        ResetOutputFormat();
        throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
            "No suitable output format found");
    }

    OutputColorRange = handle_jpeg(&OutputFormat);
    if (OutputColorRange == AVCOL_RANGE_UNSPECIFIED)
        OutputColorRange = CodecContext->color_range;
    if (OutputColorRange == AVCOL_RANGE_UNSPECIFIED)
        OutputColorRange = InputColorRange;

    OutputColorSpace = CodecContext->colorspace;
    if (OutputColorSpace == AVCOL_SPC_UNSPECIFIED)
        OutputColorSpace = InputColorSpace;

    BCSType InputType = GuessCSType(InputFormat);
    BCSType OutputType = GuessCSType(OutputFormat);

    if (InputType != OutputType) {
        if (OutputType == cRGB) {
            OutputColorSpace = AVCOL_SPC_RGB;
            OutputColorRange = AVCOL_RANGE_UNSPECIFIED;
            OutputColorPrimaries = AVCOL_PRI_UNSPECIFIED;
            OutputTransferCharateristics = AVCOL_TRC_UNSPECIFIED;
            OutputChromaLocation = AVCHROMA_LOC_UNSPECIFIED;
        } else if (OutputType == cYUV) {
            OutputColorSpace = AVCOL_SPC_BT470BG;
            OutputColorRange = AVCOL_RANGE_MPEG;
            OutputColorPrimaries = AVCOL_PRI_UNSPECIFIED;
            OutputTransferCharateristics = AVCOL_TRC_UNSPECIFIED;
            OutputChromaLocation = AVCHROMA_LOC_LEFT;
        } else if (OutputType == cGRAY) {
            OutputColorSpace = AVCOL_SPC_UNSPECIFIED;
            OutputColorRange = AVCOL_RANGE_UNSPECIFIED;
            OutputColorPrimaries = AVCOL_PRI_UNSPECIFIED;
            OutputTransferCharateristics = AVCOL_TRC_UNSPECIFIED;
            OutputChromaLocation = AVCHROMA_LOC_UNSPECIFIED;
        }
    } else {
        OutputColorPrimaries = -1;
        OutputTransferCharateristics = -1;
        OutputChromaLocation = -1;
    }

    if (InputFormat != OutputFormat ||
        TargetWidth != CodecContext->width ||
        TargetHeight != CodecContext->height ||
        InputColorSpace != OutputColorSpace ||
        InputColorRange != OutputColorRange) {
        SWS = GetSwsContext(
            Frame->width, Frame->height, InputFormat, InputColorSpace, InputColorRange,
            TargetWidth, TargetHeight, OutputFormat, OutputColorSpace, OutputColorRange,
            TargetResizer);

        if (!SWS) {
            ResetOutputFormat();
            throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_INVALID_ARGUMENT,
                "Failed to allocate SWScale context");
        }
    }

    av_freep(&SWSFrameData[0]);
    if (av_image_alloc(SWSFrameData, SWSFrameLinesize, TargetWidth, TargetHeight, OutputFormat, 4) < 0)
        throw FFMS_Exception(FFMS_ERROR_SCALING, FFMS_ERROR_ALLOCATION_FAILED,
            "Could not allocate frame with new resolution.");
}

void FFMS_VideoSource::ResetOutputFormat() {
    if (SWS) {
        sws_freeContext(SWS);
        SWS = nullptr;
    }

    TargetWidth = -1;
    TargetHeight = -1;
    TargetPixelFormats.clear();

    OutputFormat = AV_PIX_FMT_NONE;
    OutputColorSpace = AVCOL_SPC_UNSPECIFIED;
    OutputColorRange = AVCOL_RANGE_UNSPECIFIED;
    OutputColorSpaceSet = false;
    OutputColorRangeSet = false;

    OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::ResetInputFormat() {
    InputFormatOverridden = false;
    InputFormat = AV_PIX_FMT_NONE;
    InputColorSpace = AVCOL_SPC_UNSPECIFIED;
    InputColorRange = AVCOL_RANGE_UNSPECIFIED;

    ReAdjustOutputFormat(DecodeFrame);
    OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetVideoProperties() {
    VP.RFFDenominator = FormatContext->streams[VideoTrack]->time_base.num;
    VP.RFFNumerator = FormatContext->streams[VideoTrack]->time_base.den;
    if (CodecContext->codec_id == AV_CODEC_ID_H264) {
        if (VP.RFFNumerator & 1)
            VP.RFFDenominator *= 2;
        else
            VP.RFFNumerator /= 2;
    }
    VP.NumFrames = Frames.VisibleFrameCount();
    VP.TopFieldFirst = !!(DecodeFrame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
    VP.ColorSpace = CodecContext->colorspace;
    VP.ColorRange = CodecContext->color_range;
    // these pixfmt's are deprecated but still used
    if (CodecContext->pix_fmt == AV_PIX_FMT_YUVJ420P ||
        CodecContext->pix_fmt == AV_PIX_FMT_YUVJ422P ||
        CodecContext->pix_fmt == AV_PIX_FMT_YUVJ444P
        )
        VP.ColorRange = AVCOL_RANGE_JPEG;


    VP.FirstTime = ((Frames[Frames.RealFrameNumber(0)].PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
    VP.LastTime = ((Frames[Frames.RealFrameNumber(Frames.VisibleFrameCount()-1)].PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
    VP.LastEndTime = (((Frames[Frames.RealFrameNumber(Frames.VisibleFrameCount()-1)].PTS + Frames.LastDuration) * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;

    if (CodecContext->width <= 0 || CodecContext->height <= 0)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
            "Codec returned zero size video");

    // attempt to correct framerate to the proper NTSC fraction, if applicable
    CorrectRationalFramerate(&VP.FPSNumerator, &VP.FPSDenominator);
    // correct the timebase, if necessary
    CorrectTimebase(&VP, &Frames.TB);

    // Set AR variables
    VP.SARNum = CodecContext->sample_aspect_ratio.num;
    VP.SARDen = CodecContext->sample_aspect_ratio.den;

    // Set input and output formats now that we have a CodecContext
    DetectInputFormat();

    OutputFormat = InputFormat;
    OutputColorSpace = InputColorSpace;
    OutputColorRange = InputColorRange;
}

bool FFMS_VideoSource::HasPendingDelayedFrames() {
    if (Stage == DecodeStage::APPLY_DELAY) {
        if (DelayCounter > Delay) {
            --DelayCounter;
            return true;
        }
        Stage = DecodeStage::DECODE_LOOP;
    }
    return false;
}

bool FFMS_VideoSource::DecodePacket(AVPacket *Packet) {
    std::swap(DecodeFrame, LastDecodedFrame);
    ResendPacket = false;

    int Ret = avcodec_send_packet(CodecContext, Packet);
    if (Ret == AVERROR(EAGAIN)) {
        // Send queue is full, so stash packet to resend on the next call.
        DelayCounter--;
        ResendPacket = true;
    }

    Ret = avcodec_receive_frame(CodecContext, DecodeFrame);
    if (Ret != 0) {
        std::swap(DecodeFrame, LastDecodedFrame);
        if (!(Packet->flags & AV_PKT_FLAG_DISCARD) || Ret == AVERROR(EAGAIN))
            DelayCounter++;
    } else if (!!(Packet->flags & AV_PKT_FLAG_DISCARD)) {
        // If sending discarded frame when the decode buffer is not empty, caller
        // may still obtained bufferred decoded frames and the number of frames
        // in the buffer decreases.
        DelayCounter--;
    }

    if (Ret == 0 && Stage == DecodeStage::INITIALIZE)
        Stage = DecodeStage::APPLY_DELAY;

    // H.264 (PAFF) and HEVC can have one field per packet, and decoding delay needs
    // to be adjusted accordingly.
    if (CodecContext->codec_id == AV_CODEC_ID_H264 || CodecContext->codec_id == AV_CODEC_ID_HEVC) {
        if (!!(LastDecodedFrame->flags & AV_FRAME_FLAG_INTERLACED))
            HaveSeenInterlacedFrame = true;
        if (!PAFFAdjusted && DelayCounter > Delay && HaveSeenInterlacedFrame && LastDecodedFrame->repeat_pict == 0 && Ret != 0) {
            int OldBFrameDelay = Delay - (CodecContext->thread_count - 1);
            Delay = 1 + OldBFrameDelay * 2 + (CodecContext->thread_count - 1);
            PAFFAdjusted = true;
        }
    }

    return (Ret == 0) || (DelayCounter > Delay && Stage == DecodeStage::DECODE_LOOP);
}

int FFMS_VideoSource::Seek(int n) {
    int ret = -1;

    DelayCounter = 0;
    Stage = DecodeStage::INITIALIZE;

    if (!SeekByPos || Frames[n].FilePos < 0) {
        ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].PTS, AVSEEK_FLAG_BACKWARD);
    }

    if (ret < 0 && Frames[n].FilePos >= 0) {
        ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].FilePos + PosOffset, AVSEEK_FLAG_BYTE);
        if (ret >= 0)
            SeekByPos = true;
    }

    // We always assume seeking is possible if the first seek succeeds
    avcodec_flush_buffers(CodecContext);
    ResendPacket = false;
    av_packet_unref(StashedPacket);

    // When it's 0 we always know what the next frame is (or more exactly should be)
    if (n == 0)
        CurrentFrame = 0;

    return ret;
}

int FFMS_VideoSource::ReadFrame(AVPacket *pkt) {
    int ret = av_read_frame(FormatContext, pkt);
    if (ret >= 0 || ret == AVERROR(EOF)) return ret;

    // Lavf reports the beginning of the actual video data as the packet's
    // position, but the reader requires the header, so we end up seeking
    // to the wrong position. Wait until a read actual fails to adjust the
    // seek targets, so that if this ever gets fixed upstream our workaround
    // doesn't re-break it.
    if (strcmp(FormatContext->iformat->name, "yuv4mpegpipe") == 0) {
        PosOffset = -6;
        Seek(CurrentFrame);
        return av_read_frame(FormatContext, pkt);
    }
    return ret;
}

void FFMS_VideoSource::Free() {
    av_freep(&RPUBuffer);
    av_freep(&HDR10PlusBuffer);
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);
    if (SWS)
        sws_freeContext(SWS);
    av_freep(&SWSFrameData[0]);
    av_frame_free(&DecodeFrame);
    av_frame_free(&LastDecodedFrame);
    av_packet_free(&StashedPacket);
}

void FFMS_VideoSource::DecodeNextFrame(int64_t &AStartTime, int64_t &Pos) {
    AStartTime = -1;

    if (HasPendingDelayedFrames())
        return;

    AVPacket *Packet = av_packet_alloc();
    if (!Packet)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
            "Could not allocate packet.");

    int ret;
    if (ResendPacket) {
        // If we have a packet previously stashed due to a full input queue,
        // send it again.
        ret = 0;
        av_packet_ref(Packet, StashedPacket);
        av_packet_unref(StashedPacket);
    } else {
        ret = ReadFrame(Packet);
    }
    while (ret >= 0) {
        if (Packet->stream_index != VideoTrack) {
            av_packet_unref(Packet);
            ret = ReadFrame(Packet);
            continue;
        }

        if (AStartTime < 0)
            AStartTime = Frames.UseDTS ? Packet->dts : Packet->pts;

        if (Pos < 0)
            Pos = Packet->pos;

        bool FrameFinished = DecodePacket(Packet);
        if (ResendPacket)
            av_packet_ref(StashedPacket, Packet);
        av_packet_unref(Packet);
        if (FrameFinished) {
            av_packet_free(&Packet);
            return;
        }

        if (ResendPacket) {
            ret = 0;
            av_packet_ref(Packet, StashedPacket);
            av_packet_unref(StashedPacket);
        } else {
            ret = ReadFrame(Packet);
        }
    }
    if (IsIOError(ret)) {
        char err[1024];
        av_strerror(ret, err, 1024);
        std::string serr(err);
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_FILE_READ,
            "Failed to read packet: " + serr);
    }

    // Flush final frames
    DecodePacket(Packet);
    av_packet_free(&Packet);
}

bool FFMS_VideoSource::SeekTo(int n, int SeekOffset) {
    // The semantics here are basically "return true if we don't know exactly where our seek ended up (destination isn't frame 0)"
    if (SeekMode >= 0) {
        int TargetFrame = n + SeekOffset;
        if (TargetFrame < 0)
            throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_UNKNOWN,
                "Frame accurate seeking is not possible in this file");

        // Seeking too close to the end of the stream can result in a different decoder delay since
        // frames are returned as soon as draining starts, so avoid this to keep the delay predictable.
        // Is the +1 necessary here? Not sure, but let's keep it to be safe.
        int EndOfStreamDist = CodecContext->has_b_frames + 1;

        TargetFrame = std::min(TargetFrame, Frames.RealFrameNumber(std::max(0, VP.NumFrames - 1 - EndOfStreamDist)));

        if (SeekMode < 3)
            TargetFrame = Frames.FindClosestVideoKeyFrame(TargetFrame);

        if (SeekMode == 0) {
            if (n < CurrentFrame) {
                Seek(Frames[0].OriginalPos);
            }
        } else {
            // 10 frames is used as a margin to prevent excessive seeking since the predicted best keyframe isn't always selected by avformat
            if (n < CurrentFrame || TargetFrame > CurrentFrame + 10 || (SeekMode == 3 && n > CurrentFrame + 10)) {
                Seek(TargetFrame);
                return true;
            }
        }
    } else if (n < CurrentFrame) {
        throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_INVALID_ARGUMENT,
            "Non-linear access attempted");
    }
    return false;
}

FFMS_Frame *FFMS_VideoSource::GetFrame(int n) {
    GetFrameCheck(n);
    n = Frames.RealFrameNumber(n);

    if (LastFrameNum == n)
        return &LocalFrame;

    int SeekOffset = 0;
    bool Seek = true;

    do {
        bool HasSeeked = false;
        if (Seek) {
            HasSeeked = SeekTo(n, SeekOffset);
            Seek = false;
        }

        int64_t StartTime = AV_NOPTS_VALUE, FilePos = -1;
        bool Hidden = (((unsigned) CurrentFrame < Frames.size()) && Frames[CurrentFrame].Hidden);
        if (HasSeeked || !Hidden || PAFFAdjusted)
            DecodeNextFrame(StartTime, FilePos);

        if (!HasSeeked)
            continue;

        if (StartTime == AV_NOPTS_VALUE && !Frames.HasTS) {
            if (FilePos >= 0) {
                CurrentFrame = Frames.FrameFromPos(FilePos);
                if (CurrentFrame >= 0)
                    continue;
            }
            // If the track doesn't have timestamps or file positions then
            // just trust that we got to the right place, since we have no
            // way to tell where we are
            else {
                CurrentFrame = n;
                continue;
            }
        }

        CurrentFrame = Frames.FrameFromPTS(StartTime);

        // Is the seek destination time known? Does it belong to a frame?
        if (CurrentFrame < 0) {
            if (SeekMode == 1 || StartTime < 0) {
                // No idea where we are so go back a bit further
                SeekOffset -= 10;
                Seek = true;
                continue;
            }
            CurrentFrame = Frames.ClosestFrameFromPTS(StartTime);
        }

        // We want to know the frame number that we just got out of the decoder,
        // but what we currently know is the frame number of the first packet
        // we fed into the decoder, and these can be different with open-gop or
        // aggressive (non-keyframe) seeking.
        int64_t Pos = Frames[CurrentFrame].FilePos;
        if (CurrentFrame > 0 && Pos != -1) {
            int Prev = CurrentFrame - 1;
            while (Prev >= 0 && Frames[Prev].FilePos != -1 && Frames[Prev].FilePos > Pos)
                --Prev;
            CurrentFrame = Prev + 1;
        }
    } while (++CurrentFrame <= n);

    LastFrameNum = n;
    return OutputFrame(DecodeFrame);
}
