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

#include "indexing.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}



class FFIndexMemory {
private:
	FFAudioContext *AudioContexts;
	AVFormatContext *FormatContext;
public:
	FFIndexMemory(int Tracks, FFAudioContext *&AudioContexts, AVFormatContext *&FormatContext) {
		AudioContexts = new FFAudioContext[Tracks];
		this->AudioContexts = AudioContexts;
		this->FormatContext = FormatContext;
	}

	~FFIndexMemory() {
		delete[] AudioContexts;
		av_close_input_file(FormatContext);
	}
};

FFLAVFIndexer::FFLAVFIndexer(const char *Filename, AVFormatContext *FormatContext, char *ErrorMsg, unsigned MsgSize) {
	SourceFile = Filename;
	this->FormatContext = FormatContext;
	IsIndexing = false;
	
	if (av_find_stream_info(FormatContext) < 0) {
		av_close_input_file(FormatContext);
		snprintf(ErrorMsg, MsgSize, "Couldn't find stream information");
		throw ErrorMsg;
	}
}

FFLAVFIndexer::~FFLAVFIndexer() {
	if (!IsIndexing)
		av_close_input_file(FormatContext);
}

FFIndex *FFLAVFIndexer::DoIndexing(char *ErrorMsg, unsigned MsgSize) {
	IsIndexing = true;

	// Audio stuff
	FFAudioContext *AudioContexts;
	FFIndexMemory IM = FFIndexMemory(FormatContext->nb_streams, AudioContexts, FormatContext);

	for (unsigned int i = 0; i < FormatContext->nb_streams; i++) {
		if (IndexMask & (1 << i) && FormatContext->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
			AVCodecContext *AudioCodecContext = FormatContext->streams[i]->codec;

			AVCodec *AudioCodec = avcodec_find_decoder(AudioCodecContext->codec_id);
			if (AudioCodec == NULL) {
				snprintf(ErrorMsg, MsgSize, "Audio codec not found");
				return NULL;
			}

			if (avcodec_open(AudioCodecContext, AudioCodec) < 0) {
				snprintf(ErrorMsg, MsgSize, "Could not open audio codec");
				return NULL;
			}

			AudioContexts[i].CTX = AudioCodecContext;
		} else {
			IndexMask &= ~(1 << i);
		}
	}

	//

	std::auto_ptr<FFIndex> TrackIndices(new FFIndex());
	TrackIndices->Decoder = 0;

	for (unsigned int i = 0; i < FormatContext->nb_streams; i++)
		TrackIndices->push_back(FFTrack((int64_t)FormatContext->streams[i]->time_base.num * 1000, 
		FormatContext->streams[i]->time_base.den,
		static_cast<FFMS_TrackType>(FormatContext->streams[i]->codec->codec_type)));

	AVPacket Packet, TempPacket;
	InitNullPacket(&Packet);
	InitNullPacket(&TempPacket);
	while (av_read_frame(FormatContext, &Packet) >= 0) {
		// Update progress
		if (IC) {
			if ((*IC)(FormatContext->pb->pos, FormatContext->file_size, ICPrivate)) {
				snprintf(ErrorMsg, MsgSize, "Cancelled by user");
				return NULL;
			}
		}

		// Only create index entries for video for now to save space
		if (FormatContext->streams[Packet.stream_index]->codec->codec_type == CODEC_TYPE_VIDEO) {
			(*TrackIndices)[Packet.stream_index].push_back(TFrameInfo(Packet.dts, (Packet.flags & AV_PKT_FLAG_KEY) ? 1 : 0));
		} else if (FormatContext->streams[Packet.stream_index]->codec->codec_type == CODEC_TYPE_AUDIO && (IndexMask & (1 << Packet.stream_index))) {
			(*TrackIndices)[Packet.stream_index].push_back(TFrameInfo(Packet.dts, AudioContexts[Packet.stream_index].CurrentSample, (Packet.flags & AV_PKT_FLAG_KEY) ? 1 : 0));
			AVCodecContext *AudioCodecContext = FormatContext->streams[Packet.stream_index]->codec;
			TempPacket.data = Packet.data;
			TempPacket.size = Packet.size;

			while (TempPacket.size > 0) {
				int dbsize = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
				int Ret = avcodec_decode_audio3(AudioCodecContext, DecodingBuffer, &dbsize, &TempPacket);
				if (Ret < 0) {
					if (IgnoreDecodeErrors) {
						(*TrackIndices)[Packet.stream_index].clear();
						IndexMask &= ~(1 << Packet.stream_index);					
						break;
					} else {
						snprintf(ErrorMsg, MsgSize, "Audio decoding error");
						return NULL;
					}
				}

				if (Ret > 0) {
					TempPacket.size -= Ret;
					TempPacket.data += Ret;
				}

				if (dbsize > 0)
					AudioContexts[Packet.stream_index].CurrentSample += (dbsize * 8) / (av_get_bits_per_sample_format(AudioCodecContext->sample_fmt) * AudioCodecContext->channels);

				if (DumpMask & (1 << Packet.stream_index))
					WriteAudio(AudioContexts[Packet.stream_index], TrackIndices.get(), Packet.stream_index, dbsize, ErrorMsg, MsgSize);
			}
		}

		av_free_packet(&Packet);
	}

	TrackIndices->Sort();
	return TrackIndices.release();
}

int FFLAVFIndexer::GetNumberOfTracks() {
	return FormatContext->nb_streams;
}

FFMS_TrackType FFLAVFIndexer::GetTrackType(int Track) {
	return static_cast<FFMS_TrackType>(FormatContext->streams[Track]->codec->codec_type);
}

const char *FFLAVFIndexer::GetTrackCodec(int Track) { 
	return (avcodec_find_decoder(FormatContext->streams[Track]->codec->codec_id))->long_name;
}
