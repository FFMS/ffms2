//  Copyright (c) 2010 Thomas Goyne <tgoyne@gmail.com>
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

#include "indexing.h"

#include <algorithm>
#include <cassert>

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace {
#define MAPPER(m, n) OptionMapper<FFMS_ResampleOptions>(n, &FFMS_ResampleOptions::m)
    OptionMapper<FFMS_ResampleOptions> resample_options[] = {
        MAPPER(ChannelLayout,          "out_channel_layout"),
        MAPPER(SampleFormat,           "out_sample_fmt"),
        MAPPER(SampleRate,             "out_sample_rate"),
        MAPPER(MixingCoefficientType,  "mix_coeff_type"),
        MAPPER(CenterMixLevel,         "center_mix_level"),
        MAPPER(SurroundMixLevel,       "surround_mix_level"),
        MAPPER(LFEMixLevel,            "lfe_mix_level"),
        MAPPER(Normalize,              "normalize_mix_level"),
        MAPPER(ForceResample,          "force_resampling"),
        MAPPER(ResampleFilterSize,     "filter_size"),
        MAPPER(ResamplePhaseShift,     "phase_shift"),
        MAPPER(LinearInterpolation,    "linear_interp"),
        MAPPER(CutoffFrequencyRatio,   "cutoff"),
        MAPPER(MatrixedStereoEncoding, "matrix_encoding"),
        MAPPER(FilterType,             "filter_type"),
        MAPPER(KaiserBeta,             "kaiser_beta"),
        MAPPER(DitherMethod,           "dither_method")
    };
#undef MAPPER
}

FFMS_AudioSource::FFMS_AudioSource(const char *SourceFile, FFMS_Index &Index, int Track, int DelayMode, int FillGaps, double DrcScale)
    : LastValidTS(AV_NOPTS_VALUE), SourceFile(SourceFile), ResampleContext{ swr_alloc() }, TrackNumber(Track), DrcScale(DrcScale) {
    try {
        if (FillGaps < -1 || FillGaps > 1)
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Invalid gap fill mode");

        if (Track < 0 || Track >= static_cast<int>(Index.size()))
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Out of bounds track index selected");

        if (Index[Track].TT != FFMS_TYPE_AUDIO)
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Not an audio track");

        if (Index[Track].empty())
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
                "Audio track contains no audio frames");

        if (!Index.CompareFileSignature(SourceFile))
            throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
                "The index does not match the source file");

        Frames = Index[Track];
        LAVFOpts = Index.LAVFOpts;

        DecodeFrame = av_frame_alloc();
        if (!DecodeFrame)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Couldn't allocate frame");
        OpenFile();

        if (FillGaps == 1 || (FillGaps == -1 && (!strcmp(FormatContext->iformat->name, "flv"))))
            Frames.FillAudioGaps();

        if (Frames.back().PTS == Frames.front().PTS)
            SeekOffset = -1;
        else
            SeekOffset = 10;

        Init(Index, DelayMode);
    } catch (...) {
        Free();
        throw;
    }
}


#define EXCESSIVE_CACHE_SIZE 400

void FFMS_AudioSource::Init(const FFMS_Index &Index, int DelayMode) {
    // Decode the first packet to ensure all properties are initialized
    // Don't cache it since it might be in the wrong format
    for (size_t i = 0; i < Frames.size(); i++) {
        if (DecodeNextBlock())
            break;
    }

    // Read properties of the audio which may not be available until the first
    // frame has been decoded
    FillAP(AP, CodecContext, Frames);

    if (AP.SampleRate <= 0 || AP.BitsPerSample <= 0)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
            "Codec returned zero size audio");

    auto opt = CreateResampleOptions();
    SetOutputFormat(*opt);

    if (DelayMode < FFMS_DELAY_NO_SHIFT)
        throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
            "Bad audio delay compensation mode");

    if (DelayMode == FFMS_DELAY_NO_SHIFT) return;

    if (DelayMode > (signed)Index.size())
        throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
            "Out of bounds track index selected for audio delay compensation");

    if (DelayMode >= 0 && Index[DelayMode].TT != FFMS_TYPE_VIDEO)
        throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
            "Audio delay compensation must be relative to a video track");

    if (DelayMode == FFMS_DELAY_FIRST_VIDEO_TRACK) {
        for (size_t i = 0; i < Index.size(); ++i) {
            if (Index[i].TT == FFMS_TYPE_VIDEO && !Index[i].empty()) {
                DelayMode = static_cast<int>(i);
                break;
            }
        }
    }

    if (DelayMode >= 0) {
        const FFMS_Track &VTrack = Index[DelayMode];
        Delay = -(VTrack[0].PTS * VTrack.TB.Num * AP.SampleRate / (VTrack.TB.Den * 1000));
    }

    if (Frames.HasTS) {
        int i = 0;
        while (Frames[i].PTS == AV_NOPTS_VALUE) ++i;
        Delay += Frames[i].PTS * Frames.TB.Num * AP.SampleRate / (Frames.TB.Den * 1000);
        for (; i > 0; --i)
            Delay -= Frames[i].SampleCount;
    }

    AP.NumSamples += Delay;
}

void FFMS_AudioSource::CacheBeginning() {
    // Nothing to do if the cache is already populated
    if (!Cache.empty()) return;

    // The first packet after a seek is often decoded incorrectly, which
    // makes it impossible to ever correctly seek back to the beginning, so
    // store the first block now

    // In addition, anything with the same PTS as the first packet can't be
    // distinguished from the first packet and so can't be seeked to, so
    // store those as well

    // Some of LAVF's splitters don't like to seek to the beginning of the
    // file (ts and?), so cache a few blocks even if PTSes are unique
    // Packet 7 is the last packet I've had be unseekable to, so cache up to
    // 10 for a bit of an extra buffer
    auto end = Cache.end();
    while (PacketNumber < Frames.size() &&
        ((Frames[0].PTS != AV_NOPTS_VALUE && Frames[PacketNumber].PTS == Frames[0].PTS) ||
            Cache.size() < 10)) {

        // Vorbis in particular seems to like having 60+ packets at the start
        // of the file with a PTS of 0, so we might need to expand the search
        // range to account for that.
        // Expanding slightly before it's strictly needed to ensure there's a
        // bit of space for an actual cache
        if (Cache.size() >= MaxCacheBlocks - 5) {
            if (MaxCacheBlocks >= EXCESSIVE_CACHE_SIZE)
                throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                    "Exceeded the search range for an initial valid audio PTS");
            MaxCacheBlocks *= 2;
        }

        DecodeNextBlock(&end);
    }
    // Store the iterator to the last element of the cache which is used for
    // correctness rather than speed, so that when looking for one to delete
    // we know how much to skip
    CacheNoDelete = Cache.end();
    --CacheNoDelete;
}

static int PopCount(int64_t v) {
    int c = 0;
    for (size_t i = 0; i < 64; i++) {
        if (v & 1)
            c++;
        v >>= 1;
    }
    return c;
}

void FFMS_AudioSource::SetOutputFormat(FFMS_ResampleOptions const& opt) {
    if (opt.SampleRate != AP.SampleRate)
        throw FFMS_Exception(FFMS_ERROR_RESAMPLING, FFMS_ERROR_UNSUPPORTED,
            "Sample rate changes are currently unsupported.");

    // Cache stores audio in the output format, so clear it and reopen the file
    Cache.clear();
    PacketNumber = 0;
    OpenFile();
    avcodec_flush_buffers(CodecContext);

    BytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(opt.SampleFormat)) * PopCount(opt.ChannelLayout);
    NeedsResample =
        opt.SampleFormat != (int)CodecContext->sample_fmt ||
        opt.SampleRate != AP.SampleRate ||
        opt.ChannelLayout != AP.ChannelLayout ||
        opt.ForceResample;

    if (!NeedsResample) return;

    SwrContext *rawCtx = swr_alloc();

    AVChannelLayout ChLayoutOut = { AV_CHANNEL_ORDER_NATIVE, PopCount(opt.ChannelLayout), static_cast<uint64_t>(opt.ChannelLayout) };
    AVChannelLayout ChLayoutIn = { AV_CHANNEL_ORDER_NATIVE, PopCount(AP.ChannelLayout), static_cast<uint64_t>(AP.ChannelLayout) };
    swr_alloc_set_opts2(&rawCtx, &ChLayoutOut, (AVSampleFormat)opt.SampleFormat, opt.SampleRate, &ChLayoutIn, CodecContext->sample_fmt, AP.SampleRate, 0, nullptr);

    FFResampleContext newContext{ rawCtx };

    SetOptions(opt, newContext.get(), resample_options);

    if (swr_init(newContext.get()))
        throw FFMS_Exception(FFMS_ERROR_RESAMPLING, FFMS_ERROR_UNKNOWN,
            "Could not open avresample context");
    newContext.swap(ResampleContext);
}

std::unique_ptr<FFMS_ResampleOptions> FFMS_AudioSource::CreateResampleOptions() const {
    auto ret = ReadOptions(ResampleContext.get(), resample_options);
    ret->SampleRate = AP.SampleRate;
    ret->SampleFormat = static_cast<FFMS_SampleFormat>(AP.SampleFormat);
    ret->ChannelLayout = AP.ChannelLayout;
    return ret;
}

void FFMS_AudioSource::ResampleAndCache(CacheIterator pos) {
    AudioBlock& block = *pos;

    size_t size = DecodeFrame->nb_samples * BytesPerSample;
    auto dst = block.Grow(size);

    uint8_t *OutPlanes[1] = { dst };

    swr_convert(ResampleContext.get(), OutPlanes, DecodeFrame->nb_samples, (const uint8_t **)DecodeFrame->extended_data, DecodeFrame->nb_samples);
}

FFMS_AudioSource::AudioBlock *FFMS_AudioSource::CacheBlock(CacheIterator &pos) {
    // If the previous block has the same Start sample as this one, then
    // we got multiple frames of audio out of a single packet and should
    // combine them
    auto block = pos;
    if (pos == Cache.begin() || (--block)->Start != CurrentSample)
        block = Cache.emplace(pos, CurrentSample);

    block->Samples += DecodeFrame->nb_samples;

    if (NeedsResample)
        ResampleAndCache(block);
    else {
        const uint8_t *data = DecodeFrame->extended_data[0];
        auto dst = block->Grow(DecodeFrame->nb_samples * BytesPerSample);
        memcpy(dst, data, DecodeFrame->nb_samples * BytesPerSample);
    }

    if (Cache.size() >= MaxCacheBlocks) {
        // Kill the oldest one
        auto min = CacheNoDelete;
        // Never drop the first one as the first packet decoded after a seek
        // is often decoded incorrectly and we can't seek to before the first one
        ++min;
        for (auto it = min; it != Cache.end(); ++it)
            if (it->Age < min->Age) min = it;
        if (min == pos) ++pos;
        Cache.erase(min);
    }
    return &*block;
}

int FFMS_AudioSource::DecodeNextBlock(CacheIterator *pos) {
    CurrentFrame = &Frames[PacketNumber];

    AVPacket *Packet = av_packet_alloc();
    if (!Packet)
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
            "Could not allocate packet.");
    if (!ReadPacket(Packet)) {
        av_packet_free(&Packet);
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_UNKNOWN,
            "ReadPacket unexpectedly failed to read a packet");
    }

    // ReadPacket may have changed the packet number
    CurrentFrame = &Frames[PacketNumber];
    CurrentSample = CurrentFrame->SampleStart;

    int NumberOfSamples = 0;
    AudioBlock *CachedBlock = nullptr;
    
    int Ret = avcodec_send_packet(CodecContext, Packet);
    av_packet_unref(Packet);
    av_packet_free(&Packet);

    av_frame_unref(DecodeFrame);
    Ret = avcodec_receive_frame(CodecContext, DecodeFrame);
    if (Ret == 0) {
        //FIXME, is DecodeFrame->nb_samples > 0 always true for decoded frames? I can't be bothered to find out
        NumberOfSamples += DecodeFrame->nb_samples;
        if (DecodeFrame->nb_samples > 0) {
            if (pos)
                CachedBlock = CacheBlock(*pos);
        }
    }

    // Zero sample packets aren't included in the index
    if (!NumberOfSamples)
        return NumberOfSamples;
    ++PacketNumber;

    // Add padding after the packet, if needed
    if (!CachedBlock || CachedBlock->Samples == CurrentFrame->SampleCount)
        return NumberOfSamples;

    const int64_t MissingSamples = static_cast<int64_t>(CurrentFrame->SampleCount - CachedBlock->Samples);
    // This can apparently happen in some rare circumstances, caused by inaccurate seeking?
    if (MissingSamples <= 0)
        return NumberOfSamples;
    CachedBlock->Samples += MissingSamples;
    const int64_t MissingBytes = MissingSamples * BytesPerSample;
    if (MissingSamples > 200 || MissingSamples > CachedBlock->Samples - MissingSamples)
        memset(CachedBlock->Grow(MissingBytes), 0, MissingBytes);
    else {
        auto ptr = CachedBlock->Grow(MissingBytes);
        memcpy(ptr, ptr - MissingBytes, MissingBytes);
    }
    return NumberOfSamples;
}

static bool SampleStartComp(const FrameInfo &a, const FrameInfo &b) {
    return a.SampleStart < b.SampleStart;
}

void FFMS_AudioSource::GetAudio(void *Buf, int64_t Start, int64_t Count) {
    if (Start < 0 || Start + Count > AP.NumSamples || Count < 0)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
            "Out of bounds audio samples requested");

    CacheBeginning();

    uint8_t *Dst = static_cast<uint8_t*>(Buf);

    // Apply audio delay (if any) and fill any samples before the start time with zero
    Start -= Delay;
    if (Start < 0) {
        size_t Bytes = static_cast<size_t>(BytesPerSample * FFMIN(-Start, Count));
        memset(Dst, 0, Bytes);

        Count += Start;
        // Entire request was before the start of the audio
        if (Count <= 0) return;

        Start = 0;
        Dst += Bytes;
    }

    auto it = Cache.begin();

    while (Count > 0) {
        // Find first useful cache block
        while (it != Cache.end() && it->Start + it->Samples <= Start) ++it;

        // Cache has the next block we want
        if (it != Cache.end() && it->Start <= Start) {
            int64_t SrcOffset = FFMAX(0, Start - it->Start);
            int64_t DstOffset = FFMAX(0, it->Start - Start);
            int64_t CopySamples = FFMIN(it->Samples - SrcOffset, Count - DstOffset);
            size_t Bytes = static_cast<size_t>(CopySamples * BytesPerSample);

            memcpy(Dst + DstOffset * BytesPerSample, it->Data.get() + SrcOffset * BytesPerSample, Bytes);
            Start += CopySamples;
            Count -= CopySamples;
            Dst += Bytes;
            ++it;
        }
        // Decode another block
        else {
            if (Start < CurrentSample && SeekOffset == -1)
                throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Audio stream is not seekable");

            if (SeekOffset >= 0 && (Start < CurrentSample || Start > CurrentSample + DecodeFrame->nb_samples * 5)) {
                FrameInfo f;
                f.SampleStart = Start;
                size_t NewPacketNumber = std::distance(
                    Frames.begin(),
                    std::lower_bound(Frames.begin(), Frames.end(), f, SampleStartComp));
                NewPacketNumber = NewPacketNumber > static_cast<size_t>(SeekOffset + 15)
                    ? NewPacketNumber - SeekOffset - 15
                    : 0;
                while (NewPacketNumber > 0 && !Frames[NewPacketNumber].KeyFrame) --NewPacketNumber;

                // Only seek forward if it'll actually result in moving forward
                if (Start < CurrentSample || static_cast<size_t>(NewPacketNumber) > PacketNumber) {
                    PacketNumber = NewPacketNumber;
                    CurrentSample = -1;
                    av_frame_unref(DecodeFrame);
                    avcodec_flush_buffers(CodecContext);
                    Seek();
                }
            }

            // Decode until we hit the block we want
            if (PacketNumber >= Frames.size())
                throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Seeking is severely broken");
            while (CurrentSample + CurrentFrame->SampleCount <= Start && PacketNumber < Frames.size())
                DecodeNextBlock(&it);
            if (CurrentSample > Start)
                throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Seeking is severely broken");

            // The block we want is now in the cache immediately before it
            --it;
        }
    }
}

size_t FFMS_AudioSource::GetSeekablePacketNumber(FFMS_Track const& Frames, size_t PacketNumber) {
    // Packets don't always have unique PTSes, so we may not be able to
    // uniquely identify the packet we want. This function attempts to find
    // a PTS we can seek to which will let us figure out which packet we're
    // on before we get to the packet we actually wanted

    // MatroskaAudioSource doesn't need this, as it seeks by byte offset
    // rather than PTS. LAVF theoretically can seek by byte offset, but we
    // don't use it as not all demuxers support it and it's broken in some of
    // those that claim to support it

    // However much we might wish to, we can't seek to before packet zero
    if (PacketNumber == 0) return PacketNumber;

    // Desired packet's PTS is unique, so don't do anything
    if (Frames[PacketNumber].PTS != Frames[PacketNumber - 1].PTS &&
        (PacketNumber + 1 == Frames.size() || Frames[PacketNumber].PTS != Frames[PacketNumber + 1].PTS))
        return PacketNumber;

    // When decoding, we only reliably know what packet we're at when the
    // newly parsed packet has a different PTS from the previous one. As such,
    // we walk backwards until we hit a different PTS and then seek to there,
    // so that we can then decode until we hit the PTS group we actually wanted
    // (and thereby know that we're at the first packet in the group rather
    // than whatever the splitter happened to choose)

    // This doesn't work if our desired packet has the same PTS as the first
    // packet, but this scenario should never come up anyway; we permanently
    // cache the decoded results from those packets, so there's no need to ever
    // seek to them
    int64_t PTS = Frames[PacketNumber].PTS;
    while (PacketNumber > 0 && PTS == Frames[PacketNumber].PTS)
        --PacketNumber;
    return PacketNumber;
}

void FFMS_AudioSource::OpenFile() {
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);

    LAVFOpenFile(SourceFile.c_str(), FormatContext, TrackNumber, LAVFOpts);

    auto *Codec = avcodec_find_decoder(FormatContext->streams[TrackNumber]->codecpar->codec_id);
    if (Codec == nullptr)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
            "Audio codec not found");

    CodecContext = avcodec_alloc_context3(Codec);
    if (CodecContext == nullptr)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
            "Could not allocate audio decoding context");

    if (avcodec_parameters_to_context(CodecContext, FormatContext->streams[TrackNumber]->codecpar) < 0)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
            "Could not copy audio codec parameters");

    AVDictionary *CodecDict = nullptr;
    if (Codec->id == AV_CODEC_ID_AC3 || Codec->id == AV_CODEC_ID_EAC3)
        av_dict_set(&CodecDict, "drc_scale", std::to_string(DrcScale).c_str(), 0);

    if (avcodec_open2(CodecContext, Codec, &CodecDict) < 0)
        throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
            "Could not open audio codec");

    av_dict_free(&CodecDict);
}

void FFMS_AudioSource::Free() {
    av_frame_free(&DecodeFrame);
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);
}

FFMS_AudioSource::~FFMS_AudioSource() {
    Free();
}

int64_t FFMS_AudioSource::FrameTS(size_t Packet) const {
    return Frames.HasTS ? Frames[Packet].PTS : Frames[Packet].FilePos;
}

void FFMS_AudioSource::Seek() {
    size_t TargetPacket = GetSeekablePacketNumber(Frames, PacketNumber);
    LastValidTS = AV_NOPTS_VALUE;

    int Flags = Frames.HasTS ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE;

    if (av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags) < 0)
        av_seek_frame(FormatContext, TrackNumber, FrameTS(TargetPacket), Flags | AVSEEK_FLAG_ANY);

    if (TargetPacket != PacketNumber) {
        // Decode until the PTS changes so we know where we are
        int64_t LastPTS = FrameTS(PacketNumber);
        while (LastPTS == FrameTS(PacketNumber)) DecodeNextBlock();
    }
}

bool FFMS_AudioSource::ReadPacket(AVPacket *Packet) {
    while (av_read_frame(FormatContext, Packet) >= 0) {
        if (Packet->stream_index == TrackNumber) {
            // Required because not all audio packets, especially in ogg, have a pts. Use the previous valid packet's pts instead.
            if (Packet->pts == AV_NOPTS_VALUE)
                Packet->pts = LastValidTS;
            else
                LastValidTS = Packet->pts;

            // This only happens if a really shitty demuxer seeks to a packet without pts *hrm* ogg *hrm* so read until a valid pts is reached
            int64_t PacketTS = Frames.HasTS ? Packet->pts : Packet->pos;
            if (PacketTS != AV_NOPTS_VALUE) {
                while (PacketNumber > 0 && FrameTS(PacketNumber) > PacketTS) --PacketNumber;
                while (FrameTS(PacketNumber) < PacketTS) ++PacketNumber;
                return true;
            }
        }
        av_packet_unref(Packet);
    }
    return false;
}

