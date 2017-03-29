//  Copyright (c) 2007-2015 Fredrik Mellbin
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

#include "indexing.h"

#include "track.h"
#include "videoutils.h"
#include "zipfile.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/sha.h>
}

#define INDEXID 0x53920873
#define INDEX_VERSION 3

SharedAVContext::~SharedAVContext() {
    avcodec_free_context(&CodecContext);
    if (Parser)
        av_parser_close(Parser);
}

void FFMS_Index::CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]) {
    FileHandle file(Filename, "rb", FFMS_ERROR_INDEX, FFMS_ERROR_FILE_READ);

    std::unique_ptr<AVSHA, decltype(&av_free)> ctx{ av_sha_alloc(), av_free };
    av_sha_init(ctx.get(), 160);

    try {
        *Filesize = file.Size();
        std::vector<char> FileBuffer(static_cast<size_t>(std::min<int64_t>(1024 * 1024, *Filesize)));
        size_t BytesRead = file.Read(FileBuffer.data(), FileBuffer.size());
        av_sha_update(ctx.get(), reinterpret_cast<const uint8_t*>(FileBuffer.data()), BytesRead);

        if (*Filesize > static_cast<int64_t>(FileBuffer.size())) {
            file.Seek(*Filesize - static_cast<int64_t>(FileBuffer.size()), SEEK_SET);
            BytesRead = file.Read(FileBuffer.data(), FileBuffer.size());
            av_sha_update(ctx.get(), reinterpret_cast<const uint8_t*>(FileBuffer.data()), BytesRead);
        }
    } catch (...) {
        av_sha_final(ctx.get(), Digest);
        throw;
    }
    av_sha_final(ctx.get(), Digest);
}

void FFMS_Index::Finalize(std::vector<SharedAVContext> const& video_contexts) {
    for (size_t i = 0, end = size(); i != end; ++i) {
        FFMS_Track& track = (*this)[i];
        track.FinalizeTrack();

        if (track.TT != FFMS_TYPE_VIDEO) continue;

        if (video_contexts[i].CodecContext && video_contexts[i].CodecContext->has_b_frames) {
            track.MaxBFrames = video_contexts[i].CodecContext->has_b_frames;
            continue;
        }

        // Whether or not has_b_frames gets set during indexing seems
        // to vary based on version of FFmpeg/Libav, so do an extra
        // check for b-frames if it's 0.
        for (size_t f = 0; f < track.size(); ++f) {
            if (track[f].FrameType == AV_PICTURE_TYPE_B) {
                track.MaxBFrames = 1;
                break;
            }
        }
    }
}

bool FFMS_Index::CompareFileSignature(const char *Filename) {
    int64_t CFilesize;
    uint8_t CDigest[20];
    CalculateFileSignature(Filename, &CFilesize, CDigest);
    return (CFilesize == Filesize && !memcmp(CDigest, Digest, sizeof(Digest)));
}

void FFMS_Index::WriteIndex(ZipFile &zf) {
    // Write the index file header
    zf.Write<uint32_t>(INDEXID);
    zf.Write<uint32_t>(FFMS_VERSION);
    zf.Write<uint16_t>(INDEX_VERSION);
    zf.Write<uint32_t>(size());
    zf.Write<uint32_t>(ErrorHandling);
    zf.Write<uint32_t>(avutil_version());
    zf.Write<uint32_t>(avformat_version());
    zf.Write<uint32_t>(avcodec_version());
    zf.Write<uint32_t>(swscale_version());
    zf.Write<int64_t>(Filesize);
    zf.Write(Digest);

    for (size_t i = 0; i < size(); ++i)
        at(i).Write(zf);

    zf.Finish();
}

void FFMS_Index::WriteIndexFile(const char *IndexFile) {
    ZipFile zf(IndexFile, "wb");

    WriteIndex(zf);
}

uint8_t *FFMS_Index::WriteIndexBuffer(size_t *Size) {
    ZipFile zf;

    WriteIndex(zf);

    return zf.GetBuffer(Size);
}

void FFMS_Index::ReadIndex(ZipFile &zf, const char *IndexFile) {
    // Read the index file header
    if (zf.Read<uint32_t>() != INDEXID)
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("'") + IndexFile + "' is not a valid index file");

    if (zf.Read<uint32_t>() != FFMS_VERSION)
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("'") + IndexFile + "' was not created with the expected FFMS2 version");

    if (zf.Read<uint16_t>() != INDEX_VERSION)
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("'") + IndexFile + "' is not the expected index version");

    uint32_t Tracks = zf.Read<uint32_t>();
    ErrorHandling = zf.Read<uint32_t>();

    if (zf.Read<uint32_t>() != avutil_version() ||
        zf.Read<uint32_t>() != avformat_version() ||
        zf.Read<uint32_t>() != avcodec_version() ||
        zf.Read<uint32_t>() != swscale_version())
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("A different FFmpeg build was used to create '") + IndexFile + "'");

    Filesize = zf.Read<int64_t>();
    zf.Read(Digest, sizeof(Digest));

    reserve(Tracks);
    try {
        for (size_t i = 0; i < Tracks; ++i)
            emplace_back(zf);
    } catch (FFMS_Exception const&) {
        throw;
    } catch (...) {
        throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
            std::string("Unknown error while reading index information in '") + IndexFile + "'");
    }
}

FFMS_Index::FFMS_Index(const char *IndexFile) {
    ZipFile zf(IndexFile, "rb");

    ReadIndex(zf, IndexFile);
}

FFMS_Index::FFMS_Index(const uint8_t *Buffer, size_t Size) {
    ZipFile zf(Buffer, Size);

    ReadIndex(zf, "User supplied buffer");
}

FFMS_Index::FFMS_Index(int64_t Filesize, uint8_t Digest[20], int ErrorHandling)
    : ErrorHandling(ErrorHandling)
    , Filesize(Filesize) {
    memcpy(this->Digest, Digest, sizeof(this->Digest));
}

void FFMS_Indexer::SetIndexTrack(int Track, bool Index) {
    if (Track < 0 || Track >= GetNumberOfTracks())
        return;
    if (Index)
        IndexMask.insert(Track);
    else
        IndexMask.erase(Track);
};

void FFMS_Indexer::SetIndexTrackType(int TrackType, bool Index) {
    for (int i = 0; i < GetNumberOfTracks(); i++) {
        if (GetTrackType(i) == TrackType) {
            if (Index)
                IndexMask.insert(i);
            else
                IndexMask.erase(i);
        }
    }
}

void FFMS_Indexer::SetErrorHandling(int ErrorHandling_) {
    if (ErrorHandling_ != FFMS_IEH_ABORT && ErrorHandling_ != FFMS_IEH_CLEAR_TRACK &&
        ErrorHandling_ != FFMS_IEH_STOP_TRACK && ErrorHandling_ != FFMS_IEH_IGNORE)
        throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_INVALID_ARGUMENT,
            "Invalid error handling mode specified");
    ErrorHandling = ErrorHandling_;
}

void FFMS_Indexer::SetProgressCallback(TIndexCallback IC_, void *ICPrivate_) {
    IC = IC_;
    ICPrivate = ICPrivate_;
}

FFMS_Indexer *CreateIndexer(const char *Filename) {
    return new FFMS_Indexer(Filename);
}

FFMS_Indexer::FFMS_Indexer(const char *Filename)
    : SourceFile(Filename) {
    try {
        if (avformat_open_input(&FormatContext, Filename, nullptr, nullptr) != 0)
            throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
                std::string("Can't open '") + Filename + "'");

        FFMS_Index::CalculateFileSignature(Filename, &Filesize, Digest);

        if (avformat_find_stream_info(FormatContext, nullptr) < 0) {
            avformat_close_input(&FormatContext);
            throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
                "Couldn't find stream information");
        }

        for (unsigned int i = 0; i < FormatContext->nb_streams; i++)
            if (FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                IndexMask.insert(i);

        DecodeFrame = av_frame_alloc();
        if (!DecodeFrame)
            throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
                "Couldn't allocate frame");
    } catch (...) {
        Free();
        throw;
    }
}

uint32_t FFMS_Indexer::IndexAudioPacket(int Track, AVPacket *Packet, SharedAVContext &Context, FFMS_Index &TrackIndices) {
    AVCodecContext *CodecContext = Context.CodecContext;
    int64_t StartSample = Context.CurrentSample;
    int Ret = avcodec_send_packet(CodecContext, Packet);
    if (Ret != 0) {
        if (ErrorHandling == FFMS_IEH_ABORT) {
            throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING, "Audio decoding error");
        } else if (ErrorHandling == FFMS_IEH_CLEAR_TRACK) {
            TrackIndices[Track].clear();
            IndexMask.erase(Track);
        } else if (ErrorHandling == FFMS_IEH_STOP_TRACK) {
            IndexMask.erase(Track);
        }
    }

    while (true) {
        av_frame_unref(DecodeFrame);
        Ret = avcodec_receive_frame(CodecContext, DecodeFrame);
        if (Ret == 0) {
            CheckAudioProperties(Track, CodecContext);
            Context.CurrentSample += DecodeFrame->nb_samples;
        } else if (Ret == AVERROR_EOF || Ret == AVERROR(EAGAIN)) {
            break;
        } else {
            if (ErrorHandling == FFMS_IEH_ABORT) {
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING, "Audio decoding error");
            } else if (ErrorHandling == FFMS_IEH_CLEAR_TRACK) {
                TrackIndices[Track].clear();
                IndexMask.erase(Track);
            } else if (ErrorHandling == FFMS_IEH_STOP_TRACK) {
                IndexMask.erase(Track);
            }
        }
    }

    return static_cast<uint32_t>(Context.CurrentSample - StartSample);
}

static const char *GetLAVCSampleFormatName(AVSampleFormat s) {
    switch (s) {
    case AV_SAMPLE_FMT_U8:  return "8-bit unsigned integer";
    case AV_SAMPLE_FMT_S16: return "16-bit signed integer";
    case AV_SAMPLE_FMT_S32: return "32-bit signed integer";
    case AV_SAMPLE_FMT_FLT: return "Single-precision floating point";
    case AV_SAMPLE_FMT_DBL: return "Double-precision floating point";
    default:                return "Unknown";
    }
}

void FFMS_Indexer::CheckAudioProperties(int Track, AVCodecContext *Context) {
    auto it = LastAudioProperties.find(Track);
    if (it == LastAudioProperties.end()) {
        FFMS_AudioProperties &AP = LastAudioProperties[Track];
        AP.SampleRate = Context->sample_rate;
        AP.SampleFormat = Context->sample_fmt;
        AP.Channels = Context->channels;
    } else if (it->second.SampleRate != Context->sample_rate ||
        it->second.SampleFormat != Context->sample_fmt ||
        it->second.Channels != Context->channels) {
        std::ostringstream buf;
        buf <<
            "Audio format change detected. This is currently unsupported."
            << " Channels: " << it->second.Channels << " -> " << Context->channels << ";"
            << " Sample rate: " << it->second.SampleRate << " -> " << Context->sample_rate << ";"
            << " Sample format: " << GetLAVCSampleFormatName((AVSampleFormat)it->second.SampleFormat) << " -> "
            << GetLAVCSampleFormatName(Context->sample_fmt);
        throw FFMS_Exception(FFMS_ERROR_UNSUPPORTED, FFMS_ERROR_DECODING, buf.str());
    }
}

void FFMS_Indexer::ParseVideoPacket(SharedAVContext &VideoContext, AVPacket &pkt, int *RepeatPict, int *FrameType, bool *Invisible) {
    if (VideoContext.Parser) {
        uint8_t *OB;
        int OBSize;
        av_parser_parse2(VideoContext.Parser,
            VideoContext.CodecContext,
            &OB, &OBSize,
            pkt.data, pkt.size,
            pkt.pts, pkt.dts, pkt.pos);

        *RepeatPict = VideoContext.Parser->repeat_pict;
        *FrameType = VideoContext.Parser->pict_type;
        *Invisible = (VideoContext.Parser->repeat_pict < 0 || (pkt.flags & AV_PKT_FLAG_DISCARD));
    } else {
        *Invisible = !!(pkt.flags & AV_PKT_FLAG_DISCARD);
    }

    if (VideoContext.CodecContext->codec_id == AV_CODEC_ID_VP8)
        ParseVP8(pkt.data[0], Invisible, FrameType);
}

void FFMS_Indexer::Free() {
    av_frame_free(&DecodeFrame);
    avformat_close_input(&FormatContext);
}

FFMS_Indexer::~FFMS_Indexer() {
    Free();
}

int FFMS_Indexer::GetNumberOfTracks() {
    return FormatContext->nb_streams;
}

const char *FFMS_Indexer::GetFormatName() {
    return FormatContext->iformat->name;
}

FFMS_TrackType FFMS_Indexer::GetTrackType(int Track) {
    return static_cast<FFMS_TrackType>(FormatContext->streams[Track]->codecpar->codec_type);
}

const char *FFMS_Indexer::GetTrackCodec(int Track) {
    AVCodec *codec = avcodec_find_decoder(FormatContext->streams[Track]->codecpar->codec_id);
    return codec ? codec->name : nullptr;
}

FFMS_Index *FFMS_Indexer::DoIndexing() {
    std::vector<SharedAVContext> AVContexts(FormatContext->nb_streams);

    auto TrackIndices = make_unique<FFMS_Index>(Filesize, Digest, ErrorHandling);

    for (unsigned int i = 0; i < FormatContext->nb_streams; i++) {
        TrackIndices->emplace_back((int64_t)FormatContext->streams[i]->time_base.num * 1000,
            FormatContext->streams[i]->time_base.den,
            static_cast<FFMS_TrackType>(FormatContext->streams[i]->codecpar->codec_type));

        if (IndexMask.count(i) && FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVCodec *VideoCodec = avcodec_find_decoder(FormatContext->streams[i]->codecpar->codec_id);
            if (!VideoCodec) {
                FormatContext->streams[i]->discard = AVDISCARD_ALL;
                IndexMask.erase(i);
                continue;
            }

            AVContexts[i].CodecContext = avcodec_alloc_context3(VideoCodec);
            if (AVContexts[i].CodecContext == nullptr)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_ALLOCATION_FAILED,
                    "Could not allocate video codec context");

            if (avcodec_parameters_to_context(AVContexts[i].CodecContext, FormatContext->streams[i]->codecpar) < 0)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
                    "Could not copy video codec parameters");

            if (avcodec_open2(AVContexts[i].CodecContext, VideoCodec, nullptr) < 0)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
                    "Could not open video codec");

            AVContexts[i].Parser = av_parser_init(FormatContext->streams[i]->codecpar->codec_id);
            if (AVContexts[i].Parser)
                AVContexts[i].Parser->flags = PARSER_FLAG_COMPLETE_FRAMES;

            if (FormatContext->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                FormatContext->streams[i]->discard = AVDISCARD_ALL;
                IndexMask.erase(i);
            } else {
                IndexMask.insert(i);
            }
        } else if (IndexMask.count(i) && FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVCodec *AudioCodec = avcodec_find_decoder(FormatContext->streams[i]->codecpar->codec_id);
            if (AudioCodec == nullptr)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_UNSUPPORTED,
                    "Audio codec not found");

            AVContexts[i].CodecContext = avcodec_alloc_context3(AudioCodec);
            if (AVContexts[i].CodecContext == nullptr)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_ALLOCATION_FAILED,
                    "Could not allocate audio codec context");

            if (avcodec_parameters_to_context(AVContexts[i].CodecContext, FormatContext->streams[i]->codecpar) < 0)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
                    "Could not copy audio codec parameters");

            if (avcodec_open2(AVContexts[i].CodecContext, AudioCodec, nullptr) < 0)
                throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
                    "Could not open audio codec");

            (*TrackIndices)[i].HasTS = false;
        } else {
            FormatContext->streams[i]->discard = AVDISCARD_ALL;
            IndexMask.erase(i);
        }
    }

    AVPacket Packet;
    InitNullPacket(Packet);
    std::vector<int64_t> LastValidTS(FormatContext->nb_streams, ffms_av_nopts_value);
    std::vector<int> LastDuration(FormatContext->nb_streams, 0);

    int64_t filesize = avio_size(FormatContext->pb);
    while (av_read_frame(FormatContext, &Packet) >= 0) {
        // Update progress
        // FormatContext->pb can apparently be NULL when opening images.
        if (IC && FormatContext->pb) {
            if ((*IC)(FormatContext->pb->pos, filesize, ICPrivate))
                throw FFMS_Exception(FFMS_ERROR_CANCELLED, FFMS_ERROR_USER,
                    "Cancelled by user");
        }
        if (!IndexMask.count(Packet.stream_index)) {
            av_packet_unref(&Packet);
            continue;
        }

        int Track = Packet.stream_index;
        FFMS_Track &TrackInfo = (*TrackIndices)[Track];
        bool KeyFrame = !!(Packet.flags & AV_PKT_FLAG_KEY);
        ReadTS(Packet, LastValidTS[Track], (*TrackIndices)[Track].UseDTS);

        if (FormatContext->streams[Track]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            int64_t PTS = TrackInfo.UseDTS ? Packet.dts : Packet.pts;
            if (PTS == ffms_av_nopts_value) {
                if (Packet.duration == 0)
                    throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_PARSER,
                        "Invalid initial pts, dts, and duration");

                if (TrackInfo.empty())
                    PTS = 0;
                else
                    PTS = TrackInfo.back().PTS + LastDuration[Track];

                TrackInfo.HasTS = false;
            }
            LastDuration[Track] = Packet.duration;

            int RepeatPict = -1;
            int FrameType = 0;
            bool Invisible = false;
            ParseVideoPacket(AVContexts[Track], Packet, &RepeatPict, &FrameType, &Invisible);

            TrackInfo.AddVideoFrame(PTS, RepeatPict, KeyFrame,
                FrameType, Packet.pos, Invisible);
        } else if (FormatContext->streams[Track]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // For video seeking timestamps are used only if all packets have
            // timestamps, while for audio they're used if any have timestamps,
            // as it's pretty common for only some packets to have timestamps
            if (LastValidTS[Track] != ffms_av_nopts_value)
                TrackInfo.HasTS = true;

            int64_t StartSample = AVContexts[Track].CurrentSample;
            uint32_t SampleCount = IndexAudioPacket(Track, &Packet, AVContexts[Track], *TrackIndices);
            TrackInfo.SampleRate = AVContexts[Track].CodecContext->sample_rate;

            TrackInfo.AddAudioFrame(LastValidTS[Track],
                StartSample, SampleCount, KeyFrame, Packet.pos, Packet.flags & AV_PKT_FLAG_DISCARD);
        }

        av_packet_unref(&Packet);
    }

    TrackIndices->Finalize(AVContexts);
    return TrackIndices.release();
}

void FFMS_Indexer::ReadTS(const AVPacket &Packet, int64_t &TS, bool &UseDTS) {
    if (!UseDTS && Packet.pts != ffms_av_nopts_value)
        TS = Packet.pts;
    if (TS == ffms_av_nopts_value)
        UseDTS = true;
    if (UseDTS && Packet.dts != ffms_av_nopts_value)
        TS = Packet.dts;
}
