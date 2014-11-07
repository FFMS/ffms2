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

#include "audiosource.h"

#include "codectype.h"
#include "matroskareader.h"

#include <cassert>

namespace {
class FFMatroskaAudio : public FFMS_AudioSource {
	MatroskaFile *MF;
	MatroskaReaderContext MC;
	TrackInfo *TI;
	std::unique_ptr<TrackCompressionContext> TCC;
	char ErrorMessage[256];

	bool ReadPacket(AVPacket *) override;

public:
	FFMatroskaAudio(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode);
	~FFMatroskaAudio();
};

FFMatroskaAudio::FFMatroskaAudio(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode)
: FFMS_AudioSource(SourceFile, Index, Track)
, MC(SourceFile)
, TI(nullptr)
{
	if (!(MF = mkv_OpenEx(&MC.Reader, 0, 0, ErrorMessage, sizeof(ErrorMessage))))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't parse Matroska file: ") + ErrorMessage);

	TI = mkv_GetTrackInfo(MF, Track);
	assert(TI);

	if (TI->CompEnabled)
		TCC.reset(new TrackCompressionContext(MF, TI, Track));

	CodecContext.reset(avcodec_alloc_context3(nullptr), DeleteMatroskaCodecContext);
	assert(CodecContext);

	AVCodec *Codec = avcodec_find_decoder(MatroskaToFFCodecID(TI->CodecID, TI->CodecPrivate, 0, TI->AV.Audio.BitDepth));
	if (!Codec) {
		mkv_Close(MF);
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC, "Audio codec not found");
	}

	InitializeCodecContextFromMatroskaTrackInfo(TI, CodecContext);

	if (avcodec_open2(CodecContext, Codec, nullptr) < 0) {
		mkv_Close(MF);
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC, "Could not open audio codec");
	}

	Init(Index, DelayMode);
}

FFMatroskaAudio::~FFMatroskaAudio() {
	TCC.reset(); // cs_Destroy() must be called before mkv_Close()
	mkv_Close(MF);
}

bool FFMatroskaAudio::ReadPacket(AVPacket *Packet) {
	MC.ReadFrame(CurrentFrame->FilePos, CurrentFrame->FrameSize, TCC.get());
	InitNullPacket(*Packet);
	Packet->data = MC.Buffer;
	Packet->size = MC.FrameSize;
	Packet->flags = CurrentFrame->KeyFrame ? AV_PKT_FLAG_KEY : 0;

	return true;
}
}

FFMS_AudioSource *CreateMatroskaAudioSource(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode) {
    return new FFMatroskaAudio(SourceFile, Track, Index, DelayMode);
}
