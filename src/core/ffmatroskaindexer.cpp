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
#include "matroskaparser.h"
#include "stdiostream.h"

extern "C" {
#include <libavcodec/avcodec.h>
}



class MatroskaIndexMemory {
private:
	MatroskaAudioContext *AudioContexts;
	MatroskaFile *MF;
	MatroskaReaderContext *MC;
public:
	MatroskaIndexMemory(int Tracks, MatroskaAudioContext *&AudioContexts, MatroskaFile *MF, MatroskaReaderContext *MC) {
		AudioContexts = new MatroskaAudioContext[Tracks];
		this->AudioContexts = AudioContexts;
		this->MF = MF;
		this->MC = MC;
	}

	~MatroskaIndexMemory() {
		delete[] AudioContexts;
	}
};

FFMatroskaIndexer::FFMatroskaIndexer(const char *Filename, char *ErrorMsg, unsigned MsgSize) {
	memset(Codec, 0, sizeof(Codec));
	SourceFile = Filename;
	char ErrorMessage[256];

	InitStdIoStream(&MC.ST);
	MC.ST.fp = fopen(SourceFile, "rb");
	if (MC.ST.fp == NULL) {
		snprintf(ErrorMsg, MsgSize, "Can't open '%s': %s", SourceFile, strerror(errno));
		throw ErrorMsg;
	}

	setvbuf(MC.ST.fp, NULL, _IOFBF, CACHESIZE);

	MF = mkv_OpenEx(&MC.ST.base, 0, 0, ErrorMessage, sizeof(ErrorMessage));
	if (MF == NULL) {
		fclose(MC.ST.fp);
		snprintf(ErrorMsg, MsgSize, "Can't parse Matroska file: %s", ErrorMessage);
		throw ErrorMsg;
	}

	for (unsigned int i = 0; i < mkv_GetNumTracks(MF); i++) {
		TrackInfo *TI = mkv_GetTrackInfo(MF, i);
		Codec[i] = avcodec_find_decoder(MatroskaToFFCodecID(TI->CodecID, TI->CodecPrivate));
	}
}

FFMatroskaIndexer::~FFMatroskaIndexer() {
	mkv_Close(MF);
	fclose(MC.ST.fp);
}

FFIndex *FFMatroskaIndexer::DoIndexing(char *ErrorMsg, unsigned MsgSize) {
	char ErrorMessage[256];

	// Audio stuff
	MatroskaAudioContext *AudioContexts;
	MatroskaIndexMemory IM = MatroskaIndexMemory(mkv_GetNumTracks(MF), AudioContexts, MF, &MC);

	for (unsigned int i = 0; i < mkv_GetNumTracks(MF); i++) {
		TrackInfo *TI = mkv_GetTrackInfo(MF, i);
		if (IndexMask & (1 << i) && TI->Type == TT_AUDIO) {
			AVCodecContext *AudioCodecContext = avcodec_alloc_context();
			AudioCodecContext->extradata = (uint8_t *)TI->CodecPrivate;
			AudioCodecContext->extradata_size = TI->CodecPrivateSize;
			AudioContexts[i].CTX = AudioCodecContext;

			if (TI->CompEnabled) {
				AudioContexts[i].CS = cs_Create(MF, i, ErrorMessage, sizeof(ErrorMessage));
				if (AudioContexts[i].CS == NULL) {
					av_free(AudioCodecContext);
					AudioContexts[i].CTX = NULL;
					snprintf(ErrorMsg, MsgSize, "Can't create decompressor: %s", ErrorMessage);
					return NULL;
				}
			}

			AVCodec *AudioCodec = Codec[i];
			if (AudioCodec == NULL) {
				av_free(AudioCodecContext);
				AudioContexts[i].CTX = NULL;
				snprintf(ErrorMsg, MsgSize, "Audio codec not found");
				return NULL;
			}

			if (avcodec_open(AudioCodecContext, AudioCodec) < 0) {
				av_free(AudioCodecContext);
				AudioContexts[i].CTX = NULL;
				snprintf(ErrorMsg, MsgSize, "Could not open audio codec");
				return NULL;
			}
		} else {
			IndexMask &= ~(1 << i);
		}
	}

	//

	int64_t CurrentPos = ftello(MC.ST.fp);
	fseeko(MC.ST.fp, 0, SEEK_END);
	int64_t SourceSize = ftello(MC.ST.fp);
	fseeko(MC.ST.fp, CurrentPos, SEEK_SET);

	std::auto_ptr<FFIndex> TrackIndices(new FFIndex());
	TrackIndices->Decoder = 1;

	for (unsigned int i = 0; i < mkv_GetNumTracks(MF); i++)
		TrackIndices->push_back(FFTrack(mkv_TruncFloat(mkv_GetTrackInfo(MF, i)->TimecodeScale), 1000000, HaaliTrackTypeToFFTrackType(mkv_GetTrackInfo(MF, i)->Type)));

	ulonglong StartTime, EndTime, FilePos;
	unsigned int Track, FrameFlags, FrameSize;
	AVPacket TempPacket;
	InitNullPacket(&TempPacket);

	while (mkv_ReadFrame(MF, 0, &Track, &StartTime, &EndTime, &FilePos, &FrameSize, &FrameFlags) == 0) {
		// Update progress
		if (IC) {
			if ((*IC)(ftello(MC.ST.fp), SourceSize, ICPrivate)) {
				snprintf(ErrorMsg, MsgSize, "Cancelled by user");
				return NULL;
			}
		}

		// Only create index entries for video for now to save space
		if (mkv_GetTrackInfo(MF, Track)->Type == TT_VIDEO) {
			(*TrackIndices)[Track].push_back(TFrameInfo(StartTime, FilePos, FrameSize, (FrameFlags & FRAME_KF) != 0));
		} else if (mkv_GetTrackInfo(MF, Track)->Type == TT_AUDIO && (IndexMask & (1 << Track))) {
			(*TrackIndices)[Track].push_back(TFrameInfo(StartTime, AudioContexts[Track].CurrentSample, FilePos, FrameSize, (FrameFlags & FRAME_KF) != 0));
			ReadFrame(FilePos, FrameSize, AudioContexts[Track].CS, MC, ErrorMsg, MsgSize);
			AVCodecContext *AudioCodecContext = AudioContexts[Track].CTX;
			TempPacket.data = MC.Buffer;
			TempPacket.size = FrameSize;
			if ((FrameFlags & FRAME_KF) != 0)
				TempPacket.flags = AV_PKT_FLAG_KEY;
			else
				TempPacket.flags = 0;

			while (TempPacket.size > 0) {
				int dbsize = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
				int Ret = avcodec_decode_audio3(AudioCodecContext, DecodingBuffer, &dbsize, &TempPacket);
				if (Ret < 0) {
					if (IgnoreDecodeErrors) {
						(*TrackIndices)[Track].clear();
						IndexMask &= ~(1 << Track);
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
					AudioContexts[Track].CurrentSample += (dbsize * 8) / (av_get_bits_per_sample_format(AudioCodecContext->sample_fmt) * AudioCodecContext->channels);

				if (DumpMask & (1 << Track))
					WriteAudio(AudioContexts[Track], TrackIndices.get(), Track, dbsize, ErrorMsg, MsgSize);
			}
		}
	}

	TrackIndices->Sort();
	return TrackIndices.release();
}

int FFMatroskaIndexer::GetNumberOfTracks() {
	return mkv_GetNumTracks(MF);
}

FFMS_TrackType FFMatroskaIndexer::GetTrackType(int Track) {
	return HaaliTrackTypeToFFTrackType(mkv_GetTrackInfo(MF, Track)->Type);
}

const char *FFMatroskaIndexer::GetTrackCodec(int Track) {
	if (Codec[Track])
		return Codec[Track]->long_name;
	else
		return "Unsupported codec/Unknown codec name";
}
