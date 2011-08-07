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

#include "videosource.h"



void FFHaaliVideo::Free(bool CloseCodec) {
	if (CloseCodec)
		avcodec_close(CodecContext);
	if (BitStreamFilter)
		av_bitstream_filter_close(BitStreamFilter);
}

FFHaaliVideo::FFHaaliVideo(const char *SourceFile, int Track,
	FFMS_Index &Index, int Threads, FFMS_Sources SourceMode)
: Res(FFSourceResources<FFMS_VideoSource>(this)), FFMS_VideoSource(SourceFile, Index, Track, Threads) {
	BitStreamFilter = NULL;

	pMMC = HaaliOpenFile(SourceFile, SourceMode);

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

	AVCodec *Codec = NULL;
	std::swap(Codec, CodecContext->codec);
	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open video codec");

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 112)
	CodecContext->thread_count = DecodingThreads;
#else
	if (avcodec_thread_init(CodecContext, DecodingThreads))
		CodecContext->thread_count = 1;
#endif

	if (CodecContext->codec->id == CODEC_ID_H264 && SourceMode == FFMS_SOURCE_HAALIMPEG)
		BitStreamFilter = av_bitstream_filter_init("h264_mp4toannexb");

	Res.CloseCodec(true);

	// Always try to decode a frame to make sure all required parameters are known
	int64_t Dummy;
	DecodeNextFrame(&Dummy);

	VP.FPSDenominator = 1;
	VP.FPSNumerator = 30;
	VP.RFFDenominator = CodecContext->time_base.num;
	VP.RFFNumerator = CodecContext->time_base.den;
	if (CodecContext->codec_id == CODEC_ID_H264) {
		if (VP.RFFNumerator & 1)
			VP.RFFDenominator *= 2;
		else
			VP.RFFNumerator /= 2;
	}
	VP.NumFrames = Frames.size();
	VP.TopFieldFirst = DecodeFrame->top_field_first;
	VP.ColorSpace = CodecContext->colorspace;
	VP.ColorRange = CodecContext->color_range;
	// these pixfmt's are deprecated but still used
	if (
		CodecContext->pix_fmt == PIX_FMT_YUVJ420P
		|| CodecContext->pix_fmt == PIX_FMT_YUVJ422P
		|| CodecContext->pix_fmt == PIX_FMT_YUVJ444P
	)
		VP.ColorRange = AVCOL_RANGE_JPEG;

	VP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
	VP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;

	if (CodecContext->width <= 0 || CodecContext->height <= 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Codec returned zero size video");

	// Calculate the average framerate
	if (Frames.size() >= 2) {
		double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
		VP.FPSDenominator = (unsigned int)(PTSDiff  / (double)1000 / (double)(VP.NumFrames - 1) + 0.5);
		VP.FPSNumerator = 1000000;
	}

	// attempt to correct framerate to the proper NTSC fraction, if applicable
	CorrectNTSCRationalFramerate(&VP.FPSNumerator, &VP.FPSDenominator);
	// correct the timebase, if necessary
	CorrectTimebase(&VP, &Frames.TB);

	// Output the already decoded frame so it isn't wasted
	OutputFrame(DecodeFrame);

	// Set AR variables
	CComVariant pV;

	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"Video.DisplayWidth", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
		VP.SARNum  = pV.uiVal;
	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"Video.DisplayHeight", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
		VP.SARDen = pV.uiVal;
}

void FFHaaliVideo::DecodeNextFrame(int64_t *AFirstStartTime) {
	int FrameFinished = 0;
	*AFirstStartTime = -1;
	AVPacket Packet;
	InitNullPacket(Packet);

	if (InitialDecode == -1) {
		if (DelayCounter > CodecContext->has_b_frames) {
			DelayCounter--;
			goto Done;
		} else {
			InitialDecode = 0;
		}
	}

	for (;;) {
		CComPtr<IMMFrame> pMMF;
		if (pMMC->ReadFrame(NULL, &pMMF) != S_OK)
			break;

		REFERENCE_TIME  Ts, Te;
		if (*AFirstStartTime < 0 && SUCCEEDED(pMMF->GetTime(&Ts, &Te)))
			*AFirstStartTime = Ts;

		if (pMMF->GetTrack() == VideoTrack) {
			BYTE *Data = NULL;
			if (FAILED(pMMF->GetPointer(&Data)))
				goto Error;

			// align input data
			Packet.size = pMMF->GetActualDataLength();
			Packet.data = static_cast<uint8_t *>(av_mallocz(Packet.size + FF_INPUT_BUFFER_PADDING_SIZE));
			memcpy(Packet.data, Data, Packet.size);

			if (pMMF->IsSyncPoint() == S_OK)
				Packet.flags = AV_PKT_FLAG_KEY;
			else
				Packet.flags = 0;

			AVBitStreamFilterContext *bsf = BitStreamFilter;
			while (bsf) {
				av_bitstream_filter_filter(bsf, CodecContext, NULL, &Packet.data,
					&Packet.size, Data, Packet.size, Packet.flags & AV_PKT_FLAG_KEY);
				bsf = bsf->next;
			}

			avcodec_decode_video2(CodecContext, DecodeFrame, &FrameFinished, &Packet);

			av_free(Packet.data);

			if (!FrameFinished)
				DelayCounter++;
			if (DelayCounter > CodecContext->has_b_frames && !InitialDecode)
				goto Done;

			if (FrameFinished)
				goto Done;
		}
	}

	// Flush the last frames
	if (CodecContext->has_b_frames) {
		AVPacket NullPacket;
		InitNullPacket(NullPacket);
		avcodec_decode_video2(CodecContext, DecodeFrame, &FrameFinished, &NullPacket);
	}

	if (!FrameFinished)
		goto Error;

Error:
Done:
	if (InitialDecode == 1) InitialDecode = -1;
}

FFMS_Frame *FFHaaliVideo::GetFrame(int n) {
	GetFrameCheck(n);

	if (LastFrameNum == n)
		return &LocalFrame;

	bool HasSeeked = false;
	int SeekOffset = 0;

	if (n < CurrentFrame || Frames.FindClosestVideoKeyFrame(n) > CurrentFrame + 10) {
ReSeek:
		pMMC->Seek(Frames[n + SeekOffset].PTS, MMSF_PREV_KF);
		avcodec_flush_buffers(CodecContext);
		DelayCounter = 0;
		InitialDecode = 1;
		HasSeeked = true;
	}

	do {
		int64_t StartTime;
		if (CurrentFrame+CodecContext->has_b_frames >= n)
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

#endif // HAALISOURCE
