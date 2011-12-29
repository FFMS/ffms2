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

#ifdef HAALISOURCE

#include "audiosource.h"

FFHaaliAudio::FFHaaliAudio(const char *SourceFile, int Track, FFMS_Index &Index, FFMS_Sources SourceMode, int DelayMode)
: FFMS_AudioSource(SourceFile, Index, Track) {
	pMMC = HaaliOpenFile(SourceFile, SourceMode);

	int CodecPrivateSize = 0;
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

	CodecContext = InitializeCodecContextFromHaaliInfo(pBag, Frames.TCI);

	AVCodec *Codec = NULL;
	std::swap(Codec, CodecContext->codec);
	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open audio codec");

	// Can't seek by PTS if they're all the same
	if (Frames.back().PTS == Frames.front().PTS)
		SeekOffset = -1;

	Init(Index, DelayMode);
}

bool FFHaaliAudio::ReadPacket(AVPacket *Packet) {
	InitNullPacket(*Packet);

	for (;;) {
		pMMF = NULL;
		if (pMMC->ReadFrame(NULL, &pMMF) != S_OK)
			return false;

		if (pMMF->GetTrack() != TrackNumber) continue;

		REFERENCE_TIME Ts, Te;
		if (SUCCEEDED(pMMF->GetTime(&Ts, &Te))) {
			int ClosestPacket = Frames.ClosestFrameFromPTS(Ts);
			if (Frames[ClosestPacket].PTS != Frames[PacketNumber].PTS)
				PacketNumber = ClosestPacket;
		}

		if (FAILED(pMMF->GetPointer(&Packet->data)))
			return false;

		Packet->size = pMMF->GetActualDataLength();
		Packet->flags = pMMF->IsSyncPoint() == S_OK ? AV_PKT_FLAG_KEY : 0;
		return true;
	}
}

void FFHaaliAudio::Seek() {
	size_t TargetPacket = GetSeekablePacketNumber(Frames, PacketNumber);
	pMMC->Seek(Frames[TargetPacket].PTS, MMSF_PREV_KF);

	if (TargetPacket != PacketNumber) {
		// Decode until the PTS changes so we know where we are
		int64_t LastPTS = Frames[PacketNumber].PTS;
		while (LastPTS == Frames[PacketNumber].PTS) DecodeNextBlock();
	}
}

#endif // HAALISOURCE
