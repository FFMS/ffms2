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

#include "indexing.h"

#include "track.h"

extern "C" {
#include <libavutil/avutil.h>
};

namespace {
class FFLAVFIndexer : public FFMS_Indexer {
	AVFormatContext *FormatContext;
	void ReadTS(const AVPacket &Packet, int64_t &TS, bool &UseDTS);

public:
	FFLAVFIndexer(const char *Filename, AVFormatContext *FormatContext)
	: FFMS_Indexer(Filename)
	, FormatContext(FormatContext)
	{
		if (avformat_find_stream_info(FormatContext,NULL) < 0) {
			avformat_close_input(&FormatContext);
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				"Couldn't find stream information");
		}
	}

	~FFLAVFIndexer() {
		avformat_close_input(&FormatContext);
	}

	FFMS_Index *DoIndexing();

	int GetNumberOfTracks() { return FormatContext->nb_streams; }
	const char *GetFormatName() { return this->FormatContext->iformat->name; }
	FFMS_Sources GetSourceType() { return FFMS_SOURCE_LAVF; }

	FFMS_TrackType GetTrackType(int Track) {
		return static_cast<FFMS_TrackType>(FormatContext->streams[Track]->codec->codec_type);
	}

	const char *GetTrackCodec(int Track) {
		AVCodec *codec = avcodec_find_decoder(FormatContext->streams[Track]->codec->codec_id);
		return codec ? codec->name : NULL;
	}
};

FFMS_Index *FFLAVFIndexer::DoIndexing() {
	std::vector<SharedAudioContext> AudioContexts(FormatContext->nb_streams, SharedAudioContext(false));
	std::vector<SharedVideoContext> VideoContexts(FormatContext->nb_streams, SharedVideoContext(false));

	std::auto_ptr<FFMS_Index> TrackIndices(new FFMS_Index(Filesize, Digest, FFMS_SOURCE_LAVF, ErrorHandling));

	for (unsigned int i = 0; i < FormatContext->nb_streams; i++) {
		TrackIndices->push_back(FFMS_Track((int64_t)FormatContext->streams[i]->time_base.num * 1000,
			FormatContext->streams[i]->time_base.den,
			static_cast<FFMS_TrackType>(FormatContext->streams[i]->codec->codec_type)));

		if (FormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			AVCodec *VideoCodec = avcodec_find_decoder(FormatContext->streams[i]->codec->codec_id);
			if (!VideoCodec)
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_UNSUPPORTED,
					"Video codec not found");

			if (avcodec_open2(FormatContext->streams[i]->codec, VideoCodec, NULL) < 0)
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
					"Could not open video codec");

			VideoContexts[i].CodecContext = FormatContext->streams[i]->codec;
			VideoContexts[i].Parser = av_parser_init(FormatContext->streams[i]->codec->codec_id);
			if (VideoContexts[i].Parser)
				VideoContexts[i].Parser->flags = PARSER_FLAG_COMPLETE_FRAMES;

			if (FormatContext->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)
				IndexMask &= ~(1 << i);
			else
				IndexMask |= 1 << i;
		}
		else if (IndexMask & (1 << i) && FormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			AVCodecContext *AudioCodecContext = FormatContext->streams[i]->codec;

			AVCodec *AudioCodec = avcodec_find_decoder(AudioCodecContext->codec_id);
			if (AudioCodec == NULL)
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_UNSUPPORTED,
					"Audio codec not found");

			if (avcodec_open2(AudioCodecContext, AudioCodec, NULL) < 0)
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
					"Could not open audio codec");

			AudioContexts[i].CodecContext = AudioCodecContext;
			(*TrackIndices)[i].HasTS = false;
		} else {
			IndexMask &= ~(1 << i);
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
		if (!(IndexMask & (1 << Packet.stream_index))) {
			av_free_packet(&Packet);
			continue;
		}

		int Track = Packet.stream_index;
		bool KeyFrame = !!(Packet.flags & AV_PKT_FLAG_KEY);
		ReadTS(Packet, LastValidTS[Track], (*TrackIndices)[Track].UseDTS);

		if (FormatContext->streams[Track]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			int64_t PTS = LastValidTS[Track];
			if (PTS == ffms_av_nopts_value) {
				if (Packet.duration == 0)
					throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_PARSER,
						"Invalid initial pts, dts, and duration");

				if ((*TrackIndices)[Track].empty())
					PTS = 0;
				else
					PTS = (*TrackIndices)[Track].back().PTS + LastDuration[Track];

				(*TrackIndices)[Track].HasTS = false;
				LastDuration[Track] = Packet.duration;
			}

			int RepeatPict = -1;
			int FrameType = 0;
			bool Invisible = false;
			ParseVideoPacket(VideoContexts[Track], Packet, &RepeatPict, &FrameType, &Invisible);

			(*TrackIndices)[Track].AddVideoFrame(PTS, RepeatPict, KeyFrame,
				FrameType, Packet.pos, 0, Invisible);
		}
		else if (FormatContext->streams[Track]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			// For video seeking timestamps are used only if all packets have
			// timestamps, while for audio they're used if any have timestamps,
			// as it's pretty common for only some packets to have timestamps
			if (LastValidTS[Track] != ffms_av_nopts_value)
				(*TrackIndices)[Track].HasTS = true;

			int64_t StartSample = AudioContexts[Track].CurrentSample;
			uint32_t SampleCount = IndexAudioPacket(Track, &Packet, AudioContexts[Track], *TrackIndices);

			(*TrackIndices)[Track].AddAudioFrame(LastValidTS[Track],
				StartSample, SampleCount, KeyFrame, Packet.pos);
		}

		av_free_packet(&Packet);
	}

	TrackIndices->Sort();
	return TrackIndices.release();
}

void FFLAVFIndexer::ReadTS(const AVPacket &Packet, int64_t &TS, bool &UseDTS) {
	if (!UseDTS && Packet.pts != ffms_av_nopts_value)
		TS = Packet.pts;
	if (TS == ffms_av_nopts_value)
		UseDTS = true;
	if (UseDTS && Packet.dts != ffms_av_nopts_value)
		TS = Packet.dts;
}
}

FFMS_Indexer *CreateLavfIndexer(const char *Filename, AVFormatContext *FormatContext) {
	return new FFLAVFIndexer(Filename, FormatContext);
}
