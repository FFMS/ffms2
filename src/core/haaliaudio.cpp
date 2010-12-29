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

FFHaaliAudio::FFHaaliAudio(const char *SourceFile, int Track, FFMS_Index &Index, enum FFMS_Sources SourceMode, int DelayMode)
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

	CComVariant pV;

	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"CodecPrivate", &pV, NULL))) {
		CodecPrivateSize = vtSize(pV);
		CodecPrivate.resize(CodecPrivateSize);
		vtCopy(pV, FFMS_GET_VECTOR_PTR(CodecPrivate));
	}

	AVCodec *Codec = NULL;

	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"CodecID", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_BSTR))) {
		char ACodecID[2048];
		wcstombs(ACodecID, pV.bstrVal, 2000);

		int BitDepth = 0;
		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Audio.BitDepth", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			BitDepth = pV.uintVal;

		Codec = avcodec_find_decoder(MatroskaToFFCodecID(ACodecID, FFMS_GET_VECTOR_PTR(CodecPrivate), 0, BitDepth));
	}
	pU = NULL;

	if (Codec == NULL)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Audio codec not found");

	CodecContext = avcodec_alloc_context();
	CodecContext->extradata = FFMS_GET_VECTOR_PTR(CodecPrivate);
	CodecContext->extradata_size = CodecPrivateSize;

	InitializeCodecContextFromHaaliInfo(pBag, CodecContext);

	if (avcodec_open(CodecContext, Codec) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open audio codec");

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
		if (SUCCEEDED(pMMF->GetTime(&Ts, &Te)))
			PacketNumber = Frames.ClosestFrameFromPTS(Ts);

		if (FAILED(pMMF->GetPointer(&Packet->data)))
			return false;

		Packet->size = pMMF->GetActualDataLength();
		Packet->flags = pMMF->IsSyncPoint() == S_OK ? AV_PKT_FLAG_KEY : 0;
		return true;
	}
}

void FFHaaliAudio::Seek() {
	pMMC->Seek(Frames[PacketNumber].PTS, MKVF_SEEK_TO_PREV_KEYFRAME_STRICT);
}

#endif // HAALISOURCE
