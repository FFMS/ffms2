//  Copyright (c) 2011 Thomas Goyne <tgoyne@gmail.com>
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

#include "audiosource.h"

#include <cassert>

namespace {
class FFLAVFAudio : public FFMS_AudioSource {
    AVFormatContext *FormatContext = nullptr;
    int64_t LastValidTS;
    std::string SourceFile;

    bool ReadPacket(AVPacket *) override;
    void FreePacket(AVPacket *Packet) override { av_packet_unref(Packet); }
    void Seek() override;

    int64_t FrameTS(size_t Packet) const;

    void OpenFile() override {
        AVCodecContext *Context = CodecContext;

        if (Context && avcodec_is_open(Context)) {
            avcodec_free_context(&Context);
            avformat_close_input(&FormatContext);
        }

        LAVFOpenFile(SourceFile.c_str(), FormatContext, TrackNumber);

        AVCodec *Codec = avcodec_find_decoder(FormatContext->streams[TrackNumber]->codecpar->codec_id);
        if (Codec == nullptr)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Audio codec not found");

        AVCodecContext *NewContext = avcodec_alloc_context3(Codec);
        if (NewContext == nullptr)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Could not allocate audio decoding context");

        if (avcodec_parameters_to_context(NewContext, FormatContext->streams[TrackNumber]->codecpar) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Could not copy audio codec parameters");

        CodecContext.reset(NewContext);

        if (avcodec_open2(CodecContext, Codec, nullptr) < 0)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
                "Could not open audio codec");
    }

public:
    FFLAVFAudio(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode);
    ~FFLAVFAudio();
};

FFLAVFAudio::FFLAVFAudio(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode)
    : FFMS_AudioSource(SourceFile, Index, Track)
    , LastValidTS(ffms_av_nopts_value)
    , SourceFile(SourceFile) {
    try {
        OpenFile();
    } catch (...) {
        AVCodecContext *Context = CodecContext;
        avcodec_free_context(&Context);
        avformat_close_input(&FormatContext);
        throw;
    }

    if (Frames.back().PTS == Frames.front().PTS)
        SeekOffset = -1;
    else
        SeekOffset = 10;
    Init(Index, DelayMode);
}

FFLAVFAudio::~FFLAVFAudio() {
    AVCodecContext *Context = CodecContext;

    avcodec_free_context(&Context);
    avformat_close_input(&FormatContext);
}

int64_t FFLAVFAudio::FrameTS(size_t Packet) const {
    return Frames.HasTS ? Frames[Packet].PTS : Frames[Packet].FilePos;
}

void FFLAVFAudio::Seek() {
    size_t TargetPacket = GetSeekablePacketNumber(Frames, PacketNumber);
    LastValidTS = ffms_av_nopts_value;

    int Flags = Frames.HasTS ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE;

    if (av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags) < 0)
        av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags | AVSEEK_FLAG_ANY);

    if (TargetPacket != PacketNumber) {
        // Decode until the PTS changes so we know where we are
        int64_t LastPTS = FrameTS(PacketNumber);
        while (LastPTS == FrameTS(PacketNumber)) DecodeNextBlock();
    }
}

bool FFLAVFAudio::ReadPacket(AVPacket *Packet) {
    InitNullPacket(*Packet);

    while (av_read_frame(FormatContext, Packet) >= 0) {
        if (Packet->stream_index == TrackNumber) {
            // Required because not all audio packets, especially in ogg, have a pts. Use the previous valid packet's pts instead.
            if (Packet->pts == ffms_av_nopts_value)
                Packet->pts = LastValidTS;
            else
                LastValidTS = Packet->pts;

            // This only happens if a really shitty demuxer seeks to a packet without pts *hrm* ogg *hrm* so read until a valid pts is reached
            int64_t PacketTS = Frames.HasTS ? Packet->pts : Packet->pos;
            if (PacketTS != ffms_av_nopts_value) {
                while (PacketNumber > 0 && FrameTS(PacketNumber) > PacketTS) --PacketNumber;
                while (FrameTS(PacketNumber) < PacketTS) ++PacketNumber;
                return true;
            }
        }
        av_packet_unref(Packet);
    }
    return false;
}
}

FFMS_AudioSource *CreateLavfAudioSource(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode) {
    return new FFLAVFAudio(SourceFile, Track, Index, DelayMode);
}
