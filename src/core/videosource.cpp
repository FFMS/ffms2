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

    if (LastFrameWidth != CodecContext->width || LastFrameHeight != CodecContext->height || LastFramePixelFormat != CodecContext->pix_fmt) {
        if (TargetHeight > 0 && TargetWidth > 0 && !TargetPixelFormats.empty()) {
            if (!InputFormatOverridden) {
                InputFormat = AV_PIX_FMT_NONE;
                InputColorSpace = AVCOL_SPC_UNSPECIFIED;
                InputColorRange = AVCOL_RANGE_UNSPECIFIED;
            }

            ReAdjustOutputFormat();
        }
    }

    if (SWS) {
        sws_scale(SWS, Frame->data, Frame->linesize, 0, CodecContext->height, SWSFrameData, SWSFrameLinesize);
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

    LocalFrame.EncodedWidth = CodecContext->width;
    LocalFrame.EncodedHeight = CodecContext->height;
    LocalFrame.EncodedPixelFormat = CodecContext->pix_fmt;
    LocalFrame.ScaledWidth = TargetWidth;
    LocalFrame.ScaledHeight = TargetHeight;
    LocalFrame.ConvertedPixelFormat = OutputFormat;
    LocalFrame.KeyFrame = Frame->key_frame;
    LocalFrame.PictType = av_get_picture_type_char(Frame->pict_type);
    LocalFrame.RepeatPict = Frame->repeat_pict;
    LocalFrame.InterlacedFrame = Frame->interlaced_frame;
    LocalFrame.TopFieldFirst = Frame->top_field_first;   
    LocalFrame.ColorSpace = OutputColorSpace;
    LocalFrame.ColorRange = OutputColorRange;
    LocalFrame.ColorPrimaries = (OutputColorPrimaries >= 0) ? OutputColorPrimaries : CodecContext->color_primaries;
    LocalFrame.TransferCharateristics = (OutputTransferCharateristics >= 0) ? OutputTransferCharateristics : CodecContext->color_trc;
    LocalFrame.ChromaLocation = (OutputChromaLocation >= 0) ? OutputChromaLocation : CodecContext->chroma_sample_location;
    LocalFrame.HasMDMDisplayPrimaries = 0;
    LocalFrame.HasMDMMinMaxLuminance = 0;
    const AVFrameSideData *MDMSide = av_frame_get_side_data(Frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (MDMSide) {
        const AVMasteringDisplayMetadata *MDMData = reinterpret_cast<const AVMasteringDisplayMetadata *>(MDMSide->data);
        if (MDMData->has_primaries) {
            LocalFrame.HasMDMDisplayPrimaries = MDMData->has_primaries;
            for (int i = 0; i < 3; i++) {
                LocalFrame.MDMDisplayPrimariesX[i] = av_q2d(MDMData->display_primaries[i][0]);
                LocalFrame.MDMDisplayPrimariesY[i] = av_q2d(MDMData->display_primaries[i][1]);
            }
            LocalFrame.MDMWhitePointX = av_q2d(MDMData->white_point[0]);
            LocalFrame.MDMWhitePointY = av_q2d(MDMData->white_point[1]);
        }
        if (MDMData->has_luminance) {
            LocalFrame.HasMDMMinMaxLuminance = MDMData->has_luminance;
            LocalFrame.MDMMinLuminance = av_q2d(MDMData->min_luminance);
            LocalFrame.MDMMaxLuminance = av_q2d(MDMData->max_luminance);
        }
    }

    LastFrameHeight = CodecContext->height;
    LastFrameWidth = CodecContext->width;
    LastFramePixelFormat = CodecContext->pix_fmt;

    return &LocalFrame;
}

FFMS_VideoSource::FFMS_VideoSource(const char *SourceFile, FFMS_Index &Index, int Track, int Threads, int SeekMode)
    : Index(Index)
    , SeekMode(SeekMode) {

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

        if (!DecodeFrame || !LastDecodedFrame)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate dummy frame.");

        // Dummy allocations so the unallocated case doesn't have to be handled later
        if (av_image_alloc(SWSFrameData, SWSFrameLinesize, 16, 16, AV_PIX_FMT_GRAY8, 4) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate dummy frame.");

        LAVFOpenFile(SourceFile, FormatContext, VideoTrack);

        AVCodec *Codec = avcodec_find_decoder(FormatContext->streams[VideoTrack]->codecpar->codec_id);
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

        if (avcodec_open2(CodecContext, Codec, nullptr) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Could not open video codec");

        // Always try to decode a frame to make sure all required parameters are known
        int64_t DummyPTS = 0, DummyPos = 0;
        DecodeNextFrame(DummyPTS, DummyPos);

        //VP.image_type = VideoInfo::IT_TFF;
        VP.FPSDenominator = FormatContext->streams[VideoTrack]->time_base.num;
        VP.FPSNumerator = FormatContext->streams[VideoTrack]->time_base.den;

        // sanity check framerate
        if (VP.FPSDenominator > VP.FPSNumerator || VP.FPSDenominator <= 0 || VP.FPSNumerator <= 0) {
            VP.FPSDenominator = 1;
            VP.FPSNumerator = 30;
        }

        // Calculate the average framerate
        if (Frames.size() >= 2) {
            double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
            double TD = (double)(Frames.TB.Den);
            double TN = (double)(Frames.TB.Num);
            VP.FPSDenominator = (unsigned int)(PTSDiff * TN / TD * 1000.0 / (Frames.size() - 1));
            VP.FPSNumerator = 1000000;
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

        for (int i = 0; i < FormatContext->streams[VideoTrack]->nb_side_data; i++) {
            if (FormatContext->streams[VideoTrack]->side_data->type == AV_PKT_DATA_STEREO3D) {
                const AVStereo3D *StereoSideData = (const AVStereo3D *)FormatContext->streams[VideoTrack]->side_data->data;
                VP.Stereo3DType = StereoSideData->type;
                VP.Stereo3DFlags = StereoSideData->flags;
                break;
            }
        }

        // Set rotation
        VP.Rotation = 0;
        const int32_t *RotationMatrix = reinterpret_cast<const int32_t *>(av_stream_get_side_data(FormatContext->streams[VideoTrack], AV_PKT_DATA_DISPLAYMATRIX, nullptr));
        if (RotationMatrix)
            VP.Rotation = lround(av_display_rotation_get(RotationMatrix));

        if (SeekMode >= 0 && Frames.size() > 1) {
            if (Seek(0) < 0) {
                throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                    "Video track is unseekable");
            } else {
                avcodec_flush_buffers(CodecContext);
                // Since we seeked to frame 0 we need to specify that frame 0 is once again the next frame that wil be decoded
                CurrentFrame = 0;
            }
        }

        // Cannot "output" without doing all other initialization
        // This is the additional mess required for seekmode=-1 to work in a reasonable way
        OutputFrame(DecodeFrame);
    } catch (FFMS_Exception &) {
        Free();
        throw;
    }
}

FFMS_VideoSource::~FFMS_VideoSource() {
    Free();
}

FFMS_Frame *FFMS_VideoSource::GetFrameByTime(double Time) {
    int Frame = Frames.ClosestFrameFromPTS(static_cast<int64_t>((Time * 1000 * Frames.TB.Den) / Frames.TB.Num));
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
    OutputFormat = AV_PIX_FMT_NONE;

    ReAdjustOutputFormat();
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
        ReAdjustOutputFormat();
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
    if (InputColorRange == AVCOL_RANGE_UNSPECIFIED)
        InputColorRange = AVCOL_RANGE_MPEG;

    if (InputColorSpace == AVCOL_SPC_UNSPECIFIED)
        InputColorSpace = CodecContext->colorspace;
}

void FFMS_VideoSource::ReAdjustOutputFormat() {
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
            CodecContext->width, CodecContext->height, InputFormat, InputColorSpace, InputColorRange,
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

    OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::ResetInputFormat() {
    InputFormatOverridden = false;
    InputFormat = AV_PIX_FMT_NONE;
    InputColorSpace = AVCOL_SPC_UNSPECIFIED;
    InputColorRange = AVCOL_RANGE_UNSPECIFIED;

    ReAdjustOutputFormat();
    OutputFrame(DecodeFrame);
}

void FFMS_VideoSource::SetVideoProperties() {
    VP.RFFDenominator = CodecContext->time_base.num;
    VP.RFFNumerator = CodecContext->time_base.den;
    if (CodecContext->codec_id == AV_CODEC_ID_H264) {
        if (VP.RFFNumerator & 1)
            VP.RFFDenominator *= 2;
        else
            VP.RFFNumerator /= 2;
    }
    VP.NumFrames = Frames.VisibleFrameCount();
    VP.TopFieldFirst = DecodeFrame->top_field_first;
    VP.ColorSpace = CodecContext->colorspace;
    VP.ColorRange = CodecContext->color_range;
    // these pixfmt's are deprecated but still used
    if (CodecContext->pix_fmt == AV_PIX_FMT_YUVJ420P ||
        CodecContext->pix_fmt == AV_PIX_FMT_YUVJ422P ||
        CodecContext->pix_fmt == AV_PIX_FMT_YUVJ444P
        )
        VP.ColorRange = AVCOL_RANGE_JPEG;


    VP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
    VP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;

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
    if (InitialDecode == -1) {
        if (DelayCounter > FFMS_CALCULATE_DELAY) {
            --DelayCounter;
            return true;
        }
        InitialDecode = 0;
    }
    return false;
}

bool FFMS_VideoSource::DecodePacket(AVPacket *Packet) {
    std::swap(DecodeFrame, LastDecodedFrame);
    avcodec_send_packet(CodecContext, Packet);

    int Ret = avcodec_receive_frame(CodecContext, DecodeFrame);
    if (Ret != 0) {
        std::swap(DecodeFrame, LastDecodedFrame);
        DelayCounter++;
    }

    if (Ret == 0 && InitialDecode == 1)
        InitialDecode = -1;

    return (Ret == 0) || (DelayCounter > FFMS_CALCULATE_DELAY && !InitialDecode);;
}

int FFMS_VideoSource::Seek(int n) {
    int ret = -1;

    DelayCounter = 0;
	InitialDecode = 1;

    if (!SeekByPos || Frames[n].FilePos < 0) {
        ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].PTS, AVSEEK_FLAG_BACKWARD);
        if (ret >= 0)
            return ret;
    }

    if (Frames[n].FilePos >= 0) {
        ret = av_seek_frame(FormatContext, VideoTrack, Frames[n].FilePos + PosOffset, AVSEEK_FLAG_BYTE);
        if (ret >= 0)
            SeekByPos = true;
    }
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
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);
    if (SWS)
        sws_freeContext(SWS);
    av_freep(&SWSFrameData[0]);
    av_freep(&DecodeFrame);
    av_freep(&LastDecodedFrame);
}

void FFMS_VideoSource::DecodeNextFrame(int64_t &AStartTime, int64_t &Pos) {
    AStartTime = -1;

    if (HasPendingDelayedFrames())
        return;

    AVPacket Packet;
    InitNullPacket(Packet);

    while (ReadFrame(&Packet) >= 0) {
        if (Packet.stream_index != VideoTrack) {
            av_packet_unref(&Packet);
            continue;
        }

        if (AStartTime < 0)
            AStartTime = Frames.UseDTS ? Packet.dts : Packet.pts;

        if (Pos < 0)
            Pos = Packet.pos;

        bool FrameFinished = DecodePacket(&Packet);
        av_packet_unref(&Packet);
        if (FrameFinished)
            return;
    }

    // Flush final frames
    InitNullPacket(Packet);
    DecodePacket(&Packet);
}

bool FFMS_VideoSource::SeekTo(int n, int SeekOffset) {
    if (SeekMode >= 0) {
        int TargetFrame = n + SeekOffset;
        if (TargetFrame < 0)
            throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_UNKNOWN,
                "Frame accurate seeking is not possible in this file");

        if (SeekMode < 3)
            TargetFrame = Frames.FindClosestVideoKeyFrame(TargetFrame);

        if (SeekMode == 0) {
            if (n < CurrentFrame) {
                Seek(0);
                avcodec_flush_buffers(CodecContext);
                CurrentFrame = 0;
            }
        } else {
            // 10 frames is used as a margin to prevent excessive seeking since the predicted best keyframe isn't always selected by avformat
            if (n < CurrentFrame || TargetFrame > CurrentFrame + 10 || (SeekMode == 3 && n > CurrentFrame + 10)) {
                Seek(TargetFrame);
                avcodec_flush_buffers(CodecContext);
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

        if (CurrentFrame + FFMS_CALCULATE_DELAY * CodecContext->ticks_per_frame >= n || HasSeeked)
            CodecContext->skip_frame = AVDISCARD_DEFAULT;
        else
            CodecContext->skip_frame = AVDISCARD_NONREF;

        int64_t StartTime = ffms_av_nopts_value, FilePos = -1;
        DecodeNextFrame(StartTime, FilePos);

        if (!HasSeeked)
            continue;

        if (StartTime == ffms_av_nopts_value && !Frames.HasTS) {
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
