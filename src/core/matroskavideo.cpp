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

#include "videosource.h"

#include "codectype.h"

void FFMatroskaVideo::Free(bool CloseCodec) {
	TCC.reset();
	if (MF) mkv_Close(MF);
	if (CloseCodec)
		avcodec_close(CodecContext);
	av_freep(&CodecContext);
}

FFMatroskaVideo::FFMatroskaVideo(const char *SourceFile, int Track,
	FFMS_Index &Index, int Threads)
: FFMS_VideoSource(SourceFile, Index, Track, Threads)
, MF(NULL)
, MC(SourceFile)
, Res(this)
, PacketNumber(0)
{
	AVCodec *Codec = NULL;
	TrackInfo *TI = NULL;

	MF = mkv_OpenEx(&MC.Reader, 0, 0, ErrorMessage, sizeof(ErrorMessage));
	if (MF == NULL) {
		std::ostringstream buf;
		buf << "Can't parse Matroska file: " << ErrorMessage;
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
	}

	TI = mkv_GetTrackInfo(MF, VideoTrack);

	if (TI->CompEnabled)
		TCC.reset(new TrackCompressionContext(MF, TI, VideoTrack));

	CodecContext = avcodec_alloc_context3(NULL);
	CodecContext->thread_count = DecodingThreads;

	Codec = avcodec_find_decoder(MatroskaToFFCodecID(TI->CodecID, TI->CodecPrivate));
	if (Codec == NULL)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Video codec not found");

	InitializeCodecContextFromMatroskaTrackInfo(TI, CodecContext);

	if (avcodec_open2(CodecContext, Codec, NULL) < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Could not open video codec");

	Res.CloseCodec(true);

	// Always try to decode a frame to make sure all required parameters are known
	DecodeNextFrame();

	VP.FPSDenominator = 1;
	VP.FPSNumerator = 30;

	// Calculate the average framerate
	if (Frames.size() >= 2) {
		double PTSDiff = (double)(Frames.back().PTS - Frames.front().PTS);
		// Dividing by 1000 caused too much information to be lost, when CorrectNTSCRationalFramerate runs there was a possibility
		// of it outputting the wrong framerate.  We still divide by 100 to protect against the possibility of overflows.
		VP.FPSDenominator = (unsigned int)(PTSDiff * mkv_TruncFloat(TI->TimecodeScale) / 100.0 / (double)(Frames.size() - 1) + 0.5);
		VP.FPSNumerator = 10000000;
	}

	// Set the video properties from the codec context
	SetVideoProperties();

	// Output the already decoded frame so it isn't wasted
	OutputFrame(DecodeFrame);

	// Set AR variables
	VP.SARNum = TI->AV.Video.DisplayWidth * TI->AV.Video.PixelHeight;
	VP.SARDen = TI->AV.Video.DisplayHeight * TI->AV.Video.PixelWidth;

	// Set crop variables
	VP.CropLeft = TI->AV.Video.CropL;
	VP.CropRight = TI->AV.Video.CropR;
	VP.CropTop = TI->AV.Video.CropT;
	VP.CropBottom = TI->AV.Video.CropB;
}

void FFMatroskaVideo::DecodeNextFrame() {
	if (HasPendingDelayedFrames()) return;

	AVPacket Packet;
	InitNullPacket(Packet);

	while (PacketNumber < Frames.size()) {
		// The additional indirection is because the packets are stored in
		// presentation order and not decoding order, this is unnoticeable
		// in the other sources where less is done manually
		const FrameInfo &FI = Frames[Frames[PacketNumber].OriginalPos];
		MC.ReadFrame(FI.FilePos, FI.FrameSize, TCC.get());

		Packet.data = MC.Buffer;
		Packet.size = MC.FrameSize;
		Packet.flags = FI.KeyFrame ? AV_PKT_FLAG_KEY : 0;

		PacketNumber++;

		if (DecodePacket(&Packet))
			return;
	}

	FlushFinalFrames();
}

FFMS_Frame *FFMatroskaVideo::GetFrame(int n) {
	GetFrameCheck(n);
	n = Frames.RealFrameNumber(n);

	if (LastFrameNum == n)
		return &LocalFrame;

	bool HasSeeked = false;
	int ClosestKF = Frames.FindClosestVideoKeyFrame(n);
	if (CurrentFrame > n || ClosestKF > CurrentFrame + 10) {
		DelayCounter = 0;
		InitialDecode = 1;
		PacketNumber = ClosestKF;
		CurrentFrame = ClosestKF;
		FlushBuffers(CodecContext);
		HasSeeked = true;
	}

	do {
		if (CurrentFrame + FFMS_CALCULATE_DELAY >= n || HasSeeked)
			CodecContext->skip_frame = AVDISCARD_DEFAULT;
		else
			CodecContext->skip_frame = AVDISCARD_NONREF;
		DecodeNextFrame();
		CurrentFrame++;
		HasSeeked = false;
	} while (CurrentFrame <= n);

	LastFrameNum = n;
	return OutputFrame(DecodeFrame);
}
