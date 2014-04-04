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

#include "haalicommon.h"
#include "videosource.h"

namespace {
class FFHaaliVideo : public FFMS_VideoSource {
	FFCodecContext HCodecContext;
	CComPtr<IMMContainer> pMMC;
	AVBitStreamFilterContext *BitStreamFilter;
	FFSourceResources<FFMS_VideoSource> Res;

	void DecodeNextFrame(int64_t *AFirstStartTime);
	void Free(bool CloseCodec);

public:
	FFHaaliVideo(const char *SourceFile, int Track, FFMS_Index &Index, int Threads, FFMS_Sources SourceMode);
	FFMS_Frame *GetFrame(int n);
};

void FFHaaliVideo::Free(bool CloseCodec) {
	if (CloseCodec)
		avcodec_close(CodecContext);
	if (BitStreamFilter)
		av_bitstream_filter_close(BitStreamFilter);
}

FFHaaliVideo::FFHaaliVideo(const char *SourceFile, int Track, FFMS_Index &Index, int Threads, FFMS_Sources SourceMode)
: Res(FFSourceResources<FFMS_VideoSource>(this)), FFMS_VideoSource(SourceFile, Index, Track, Threads)
, pMMC(HaaliOpenFile(SourceFile, SourceMode))
, BitStreamFilter(NULL)
{
	CComPtr<IEnumUnknown> pEU;
	if (!SUCCEEDED(pMMC->EnumTracks(&pEU)))
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Failed to enumerate tracks");

	CComPtr<IUnknown> pU;
	int CurrentTrack = -1;
	while (pEU->Next(1, &pU, NULL) == S_OK && ++CurrentTrack != Track) pU = NULL;
	CComQIPtr<IPropertyBag> pBag = pU;

	if (CurrentTrack != Track || !pBag)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Failed to find track");

	HCodecContext = InitializeCodecContextFromHaaliInfo(pBag);
	CodecContext = HCodecContext;

	const AVCodec *Codec = NULL;
	std::swap(Codec, CodecContext->codec);
	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open video codec");

	CodecContext->thread_count = DecodingThreads;

	if (CodecContext->codec->id == FFMS_ID(H264) && SourceMode == FFMS_SOURCE_HAALIMPEG)
		BitStreamFilter = av_bitstream_filter_init("h264_mp4toannexb");

	Res.CloseCodec(true);

	// Always try to decode a frame to make sure all required parameters are known
	int64_t Dummy;
	DecodeNextFrame(&Dummy);

	VP.FPSDenominator = 1;
	VP.FPSNumerator = 30;

	// Calculate the average framerate
	if (Frames.size() >= 2) {
		double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
		VP.FPSDenominator = (unsigned int)(PTSDiff / 1000.0 / (double)(Frames.size() - 1) + 0.5);
		VP.FPSNumerator = 1000000;
	}

	// Set the video properties from the codec context
	SetVideoProperties();

	// Output the already decoded frame so it isn't wasted
	OutputFrame(DecodeFrame);

	// Set AR variables
	CComVariant pV;

	USHORT Num = 0, Den = 0;

	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"Video.DisplayWidth", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
		Num = pV.uiVal;
	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"Video.DisplayHeight", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
		Den = pV.uiVal;

	if (Num && Den) {
		VP.SARNum = LocalFrame.EncodedHeight * Num;
		VP.SARDen = LocalFrame.EncodedWidth * Den;
	}
}

void FFHaaliVideo::DecodeNextFrame(int64_t *AFirstStartTime) {
	*AFirstStartTime = -1;
	if (HasPendingDelayedFrames()) return;

	AVPacket Packet;
	InitNullPacket(Packet);

	for (;;) {
		CComPtr<IMMFrame> pMMF;
		if (pMMC->ReadFrame(NULL, &pMMF) != S_OK)
			break;

		if (pMMF->GetTrack() != VideoTrack)
			continue;

		REFERENCE_TIME  Ts, Te;
		if (*AFirstStartTime < 0 && SUCCEEDED(pMMF->GetTime(&Ts, &Te)))
			*AFirstStartTime = Ts;

		BYTE *Data = NULL;
		if (FAILED(pMMF->GetPointer(&Data)))
			return;

		// align input data
		Packet.size = pMMF->GetActualDataLength();
		Packet.data = static_cast<uint8_t *>(av_mallocz(Packet.size + FF_INPUT_BUFFER_PADDING_SIZE));
		uint8_t *OriginalData = Packet.data;
		memcpy(Packet.data, Data, Packet.size);
		Packet.flags = pMMF->IsSyncPoint() == S_OK ? AV_PKT_FLAG_KEY : 0;

		AVBitStreamFilterContext *bsf = BitStreamFilter;
		while (bsf) {
			av_bitstream_filter_filter(bsf, CodecContext, NULL, &Packet.data,
				&Packet.size, Data, Packet.size, Packet.flags & AV_PKT_FLAG_KEY);
			bsf = bsf->next;
		}

		bool FrameFinished = DecodePacket(&Packet);

		// av_bitstream_filter_filter() can, if it feels like it, reallocate the input buffer and change the pointer.
		// If it does that, we need to free both the input buffer we allocated ourselves and the buffer lavc allocated.
		if (Packet.data != OriginalData) {
			av_free(Packet.data);
			Packet.data = OriginalData;
		}
		av_free(Packet.data);

		if (FrameFinished)
			return;
	}

	FlushFinalFrames();
}

FFMS_Frame *FFHaaliVideo::GetFrame(int n) {
	GetFrameCheck(n);
	n = Frames.RealFrameNumber(n);

	if (LastFrameNum == n)
		return &LocalFrame;

	bool HasSeeked = false;
	int SeekOffset = 0;

	if (n < CurrentFrame || Frames.FindClosestVideoKeyFrame(n) > CurrentFrame + 10) {
ReSeek:
		pMMC->Seek(Frames[n + SeekOffset].PTS, MMSF_PREV_KF);
		FlushBuffers(CodecContext);
		DelayCounter = 0;
		InitialDecode = 1;
		HasSeeked = true;
	}

	do {
		int64_t StartTime = -1;
		if (CurrentFrame + FFMS_CALCULATE_DELAY >= n || HasSeeked)
			CodecContext->skip_frame = AVDISCARD_DEFAULT;
		else
			CodecContext->skip_frame = AVDISCARD_NONREF;
		DecodeNextFrame(&StartTime);

		if (HasSeeked) {
			HasSeeked = false;

			if (StartTime < 0 || (CurrentFrame = Frames.FrameFromPTS(StartTime)) < 0) {
				// No idea where we are so go back a bit further
				if (n + SeekOffset == 0)
					throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_UNKNOWN,
						"Frame accurate seeking is not possible in this file");

				SeekOffset -= FFMIN(20, n + SeekOffset);
				goto ReSeek;
			}
		}

		CurrentFrame++;
	} while (CurrentFrame <= n);

	LastFrameNum = n;
	return OutputFrame(DecodeFrame);
}
}

FFMS_VideoSource *CreateHaaliVideoSource(const char *SourceFile, int Track, FFMS_Index &Index, int Threads, FFMS_Sources SourceMode) {
    return new FFHaaliVideo(SourceFile, Track, Index, Threads, SourceMode);
}

#endif // HAALISOURCE
