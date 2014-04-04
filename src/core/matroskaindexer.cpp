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

#include "codectype.h"
#include "matroskareader.h"
#include "track.h"

namespace {
class FFMatroskaIndexer : public FFMS_Indexer {
	MatroskaFile *MF;
	MatroskaReaderContext MC;
	AVCodec *Codec[32];

public:
	FFMatroskaIndexer(const char *Filename);
    ~FFMatroskaIndexer() {
        mkv_Close(MF);
    }

	FFMS_Index *DoIndexing();

	int GetNumberOfTracks() { return mkv_GetNumTracks(MF); }
	FFMS_TrackType GetTrackType(int Track) { return HaaliTrackTypeToFFTrackType(mkv_GetTrackInfo(MF, Track)->Type); }
	const char *GetTrackCodec(int Track) { return Codec[Track] ? Codec[Track]->name : NULL; }
	const char *GetFormatName() { return "matroska"; }
	FFMS_Sources GetSourceType() { return FFMS_SOURCE_MATROSKA; }
};

FFMatroskaIndexer::FFMatroskaIndexer(const char *Filename)
: FFMS_Indexer(Filename)
, MC(Filename)
{
	memset(Codec, 0, sizeof(Codec));

	char ErrorMessage[256];
	MF = mkv_OpenEx(&MC.Reader, 0, 0, ErrorMessage, sizeof(ErrorMessage));
	if (MF == NULL)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't parse Matroska file: ") + ErrorMessage);

	for (unsigned int i = 0; i < mkv_GetNumTracks(MF); i++) {
		TrackInfo *TI = mkv_GetTrackInfo(MF, i);
		Codec[i] = avcodec_find_decoder(MatroskaToFFCodecID(TI->CodecID, TI->CodecPrivate, 0, TI->AV.Audio.BitDepth));
	}
}

FFMS_Index *FFMatroskaIndexer::DoIndexing() {
	std::vector<SharedAudioContext> AudioContexts(mkv_GetNumTracks(MF), SharedAudioContext(true));
	std::vector<SharedVideoContext> VideoContexts(mkv_GetNumTracks(MF), SharedVideoContext(true));

	std::auto_ptr<FFMS_Index> TrackIndices(new FFMS_Index(Filesize, Digest, FFMS_SOURCE_MATROSKA, ErrorHandling));

	for (unsigned int i = 0; i < mkv_GetNumTracks(MF); i++) {
		TrackInfo *TI = mkv_GetTrackInfo(MF, i);
		TrackIndices->push_back(FFMS_Track(mkv_TruncFloat(mkv_GetTrackInfo(MF, i)->TimecodeScale), 1000000, HaaliTrackTypeToFFTrackType(mkv_GetTrackInfo(MF, i)->Type)));

		if (!Codec[i]) continue;

		AVCodecContext *CodecContext = avcodec_alloc_context3(NULL);
		InitializeCodecContextFromMatroskaTrackInfo(TI, CodecContext);

		try {
			if (TI->Type == TT_VIDEO && (VideoContexts[i].Parser = av_parser_init(Codec[i]->id))) {
				if (avcodec_open2(CodecContext, Codec[i], NULL) < 0)
					throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
						"Could not open video codec");

				if (TI->CompEnabled)
					VideoContexts[i].TCC = new TrackCompressionContext(MF, TI, i);

				VideoContexts[i].CodecContext = CodecContext;
				VideoContexts[i].Parser->flags = PARSER_FLAG_COMPLETE_FRAMES;
			}
			else if (IndexMask & (1 << i) && TI->Type == TT_AUDIO) {
				if (avcodec_open2(CodecContext, Codec[i], NULL) < 0)
					throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
						"Could not open audio codec");

				if (TI->CompEnabled)
					AudioContexts[i].TCC = new TrackCompressionContext(MF, TI, i);

				AudioContexts[i].CodecContext = CodecContext;
			} else {
				IndexMask &= ~(1 << i);
				av_freep(&CodecContext);
			}
		}
		catch (...) {
			av_freep(&CodecContext);
			throw;
		}
	}

	ulonglong StartTime, EndTime, FilePos;
	unsigned int Track, FrameFlags, FrameSize;
	AVPacket TempPacket;
	InitNullPacket(TempPacket);

	while (mkv_ReadFrame(MF, 0, &Track, &StartTime, &EndTime, &FilePos, &FrameSize, &FrameFlags) == 0) {
		// Update progress
		if (IC && (*IC)(FilePos, Filesize, ICPrivate))
			throw FFMS_Exception(FFMS_ERROR_CANCELLED, FFMS_ERROR_USER, "Cancelled by user");

		unsigned int CompressedFrameSize = FrameSize;
		unsigned char TrackType = mkv_GetTrackInfo(MF, Track)->Type;

		TrackCompressionContext *TCC = NULL;
		if (VideoContexts[Track].Parser || (TrackType == TT_AUDIO && (IndexMask & (1 << Track)))) {
			if (TrackType == TT_VIDEO)
				TCC = VideoContexts[Track].TCC;
			else
				TCC = AudioContexts[Track].TCC;
			MC.ReadFrame(FilePos, FrameSize, TCC);
			TempPacket.data = MC.Buffer;
			TempPacket.size = MC.FrameSize;
			TempPacket.flags = FrameFlags & FRAME_KF ? AV_PKT_FLAG_KEY : 0;
		}

		if (TrackType == TT_VIDEO) {
			TempPacket.pts = TempPacket.dts = TempPacket.pos = ffms_av_nopts_value;

			int RepeatPict = -1;
			int FrameType = 0;
			bool Invisible = false;
			ParseVideoPacket(VideoContexts[Track], TempPacket, &RepeatPict, &FrameType, &Invisible);

			(*TrackIndices)[Track].AddVideoFrame(StartTime, RepeatPict,
				(FrameFlags & FRAME_KF) != 0, FrameType, FilePos,
				CompressedFrameSize, Invisible);
		} else if (TrackType == TT_AUDIO && (IndexMask & (1 << Track))) {
			int64_t StartSample = AudioContexts[Track].CurrentSample;
			uint32_t SampleCount = IndexAudioPacket(Track, &TempPacket, AudioContexts[Track], *TrackIndices);

			(*TrackIndices)[Track].AddAudioFrame(StartTime, StartSample,
				SampleCount, (FrameFlags & FRAME_KF) != 0, FilePos, CompressedFrameSize);
		}
	}

	for (size_t i = 0; i < TrackIndices->size(); ++i) {
		FFMS_Track& track = (*TrackIndices)[i];
		if (track.TT != FFMS_TYPE_VIDEO) continue;

		if (VideoContexts[i].CodecContext->has_b_frames) {
			track.MaxBFrames = VideoContexts[i].CodecContext->has_b_frames;
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

	TrackIndices->Sort();
	return TrackIndices.release();
}
}

FFMS_Indexer *CreateMatroskaIndexer(const char *Filename) {
	return new FFMatroskaIndexer(Filename);
}