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

#ifdef HAALISOURCE

#include "indexing.h"

#include "codectype.h"
#include "haalicommon.h"
#include "track.h"

#include <limits>

namespace {
class FFHaaliIndexer : public FFMS_Indexer {
	FFMS_Sources SourceMode;
	CComPtr<IMMContainer> pMMC;
	int NumTracks;
	FFMS_TrackType TrackType[32];
	CComQIPtr<IPropertyBag> PropertyBags[32];
	int64_t Duration;
	AVFormatContext *FormatContext;

public:
	FFHaaliIndexer(const char *Filename, FFMS_Sources SourceMode);
	FFMS_Index *DoIndexing();

	const char *GetTrackCodec(int Track) {
		if (!PropertyBags[Track]) return NULL;

		FFCodecContext CodecContext(InitializeCodecContextFromHaaliInfo(PropertyBags[Track]));
		if (!CodecContext || !CodecContext->codec) return NULL;
		return CodecContext->codec->name;
	}

	int GetNumberOfTracks() { return NumTracks; }
	FFMS_TrackType GetTrackType(int Track) { return TrackType[Track]; }
	const char *GetFormatName() { return SourceMode == FFMS_SOURCE_HAALIMPEG ? "mpeg" : "ogg"; }
	FFMS_Sources GetSourceType() { return SourceMode; }
};

FFHaaliIndexer::FFHaaliIndexer(const char *Filename, FFMS_Sources SourceMode)
: FFMS_Indexer(Filename)
, SourceMode(SourceMode)
, pMMC(HaaliOpenFile(Filename, SourceMode))
, NumTracks(0)
, Duration(0)
{
	for (int i = 0; i < 32; i++)
		TrackType[i] = FFMS_TYPE_UNKNOWN;

	CComQIPtr<IPropertyBag> pBag2 = pMMC;
	CComVariant pV2;
	if (SUCCEEDED(pBag2->Read(L"Duration", &pV2, NULL)) && SUCCEEDED(pV2.ChangeType(VT_UI8)))
		Duration = pV2.ullVal;

	CComPtr<IEnumUnknown> pEU;
	if (SUCCEEDED(pMMC->EnumTracks(&pEU))) {
		CComPtr<IUnknown> pU;
		while (pEU->Next(1, &pU, NULL) == S_OK) {
			CComQIPtr<IPropertyBag> pBag = pU;
			if (pBag) {
				CComVariant pV;
				if (SUCCEEDED(pBag->Read(L"Type", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4))) {
					TrackType[NumTracks] = HaaliTrackTypeToFFTrackType(pV.uintVal);
					if (TrackType[NumTracks] == FFMS_TYPE_VIDEO || TrackType[NumTracks] == FFMS_TYPE_AUDIO)
						PropertyBags[NumTracks] = pBag;
				}
			}

			pU = NULL;
			NumTracks++;
		}
	}
}

FFMS_Index *FFHaaliIndexer::DoIndexing() {
	FFCodecContext Contexts[32];
	std::vector<SharedAudioContext> AudioContexts(NumTracks, SharedAudioContext(false));
	std::vector<SharedVideoContext> VideoContexts(NumTracks, SharedVideoContext(false));

	std::auto_ptr<FFMS_Index> TrackIndices(new FFMS_Index(Filesize, Digest, SourceMode, ErrorHandling));

	for (int i = 0; i < NumTracks; i++) {
		TrackIndices->push_back(FFMS_Track(1, 1000000, TrackType[i]));
		if (!PropertyBags[i] || (TrackType[i] == FFMS_TYPE_AUDIO && !(IndexMask & (1 << i)))) continue;

		FFCodecContext CodecContext(InitializeCodecContextFromHaaliInfo(PropertyBags[i]));

		if (!CodecContext->codec)
			throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_UNSUPPORTED, "Codec not found");

		const AVCodec *Codec = NULL;
		std::swap(Codec, CodecContext->codec);
		if (avcodec_open2(CodecContext, Codec, NULL) < 0)
			throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING,
				"Could not open codec");

		if (TrackType[i] == FFMS_TYPE_VIDEO) {
			VideoContexts[i].Parser = av_parser_init(CodecContext->codec->id);
			VideoContexts[i].CodecContext = CodecContext;
			VideoContexts[i].Parser->flags = PARSER_FLAG_COMPLETE_FRAMES;

			if (CodecContext->codec->id == FFMS_ID(H264) && SourceMode == FFMS_SOURCE_HAALIMPEG)
				VideoContexts[i].BitStreamFilter = av_bitstream_filter_init("h264_mp4toannexb");
		}
		else {
			AudioContexts[i].CodecContext = CodecContext;
		}
		Contexts[i] = CodecContext;
	}

	AVPacket TempPacket;
	InitNullPacket(TempPacket);
	REFERENCE_TIME Ts, Te;
	REFERENCE_TIME MinTs = (std::numeric_limits<REFERENCE_TIME>::max)();

	for (;;) {
		CComPtr<IMMFrame> pMMF;
		if (pMMC->ReadFrame(NULL, &pMMF) != S_OK)
			break;

		HRESULT hr = pMMF->GetTime(&Ts, &Te);

		if (IC) {
			if (Duration > 0) {
				if (Ts < MinTs) MinTs = Ts;
				if (SUCCEEDED(hr)) {
					if ((*IC)(Ts - MinTs, Duration, ICPrivate))
						throw FFMS_Exception(FFMS_ERROR_CANCELLED, FFMS_ERROR_USER,
							"Cancelled by user");
				}
			} else {
				if ((*IC)(0, 1, ICPrivate))
					throw FFMS_Exception(FFMS_ERROR_CANCELLED, FFMS_ERROR_USER,
						"Cancelled by user");
			}
		}

		unsigned int Track = pMMF->GetTrack();

		// copy data into aligned and padded buffer
		TempPacket.size = pMMF->GetActualDataLength();
		TempPacket.data = static_cast<uint8_t *>(av_mallocz(TempPacket.size + FF_INPUT_BUFFER_PADDING_SIZE));
		BYTE *TempData;
		pMMF->GetPointer(&TempData);
		memcpy(TempPacket.data, TempData, TempPacket.size);
		uint8_t *OriginalData = TempPacket.data;

		if (TrackType[Track] == FFMS_TYPE_VIDEO) {
			if (VideoContexts[Track].BitStreamFilter) {
				AVBitStreamFilterContext *bsf = VideoContexts[Track].BitStreamFilter;
				while (bsf) {
					av_bitstream_filter_filter(bsf, VideoContexts[Track].CodecContext, NULL,
						&TempPacket.data, &TempPacket.size, TempPacket.data, TempPacket.size, (pMMF->IsSyncPoint() == S_OK));
					bsf = bsf->next;
				}
			}

			TempPacket.pts = TempPacket.dts = TempPacket.pos = ffms_av_nopts_value;

			int RepeatPict = -1;
			int FrameType = 0;
			bool Invisible = false;
			ParseVideoPacket(VideoContexts[Track], TempPacket, &RepeatPict, &FrameType, &Invisible);

			(*TrackIndices)[Track].AddVideoFrame(Ts, RepeatPict,
				pMMF->IsSyncPoint() == S_OK, FrameType, 0, 0, Invisible);

			// av_bitstream_filter_filter() can, if it feels like it, reallocate the input buffer and change the pointer.
			// If it does that, we need to free both the input buffer we allocated ourselves and the buffer lavc allocated.
			if (TempPacket.data != OriginalData) {
				av_free(TempPacket.data);
				TempPacket.data = OriginalData;
			}
		} else if (TrackType[Track] == FFMS_TYPE_AUDIO && (IndexMask & (1 << Track))) {
			TempPacket.flags = pMMF->IsSyncPoint() == S_OK ? AV_PKT_FLAG_KEY : 0;

			int64_t StartSample = AudioContexts[Track].CurrentSample;
			uint32_t SampleCount = IndexAudioPacket(Track, &TempPacket, AudioContexts[Track], *TrackIndices);

			(*TrackIndices)[Track].AddAudioFrame(Ts, StartSample, SampleCount, pMMF->IsSyncPoint() == S_OK);
		}

		av_free(TempPacket.data);
	}

	TrackIndices->Sort();
	return TrackIndices.release();
}
}

FFMS_Indexer *CreateHaaliIndexer(const char *Filename, FFMS_Sources SourceMode) {
	return new FFHaaliIndexer(Filename, SourceMode);
}
#endif
