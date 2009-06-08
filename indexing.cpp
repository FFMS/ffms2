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

SharedAudioContext::SharedAudioContext() {
	W64W = NULL;
	CTX = NULL;
	CurrentSample = 0;
}

SharedAudioContext::~SharedAudioContext() {
	delete W64W;
	if (CTX)
		avcodec_close(CTX);
}

MatroskaAudioContext::MatroskaAudioContext() {
	CS = NULL;
	CodecPrivate = NULL;
}

MatroskaAudioContext::~MatroskaAudioContext() {
	if (CTX) {
		av_free(CTX);
	}

	if (CS)
		cs_Destroy(CS);
	delete[] CodecPrivate;
}

void FFIndexer::SetIndexMask(int IndexMask) {
	this->IndexMask = IndexMask;
}

void FFIndexer::SetDumpMask(int DumpMask) {
	this->DumpMask = DumpMask;
}

void FFIndexer::SetIgnoreDecodeErrors(bool IgnoreDecodeErrors) {
	this->IgnoreDecodeErrors = IgnoreDecodeErrors;
}

void FFIndexer::SetProgressCallback(TIndexCallback IC, void *ICPrivate) {
	this->IC = IC; 
	this->ICPrivate = ICPrivate;
}

void FFIndexer::SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate) {
	this->ANC = ANC;
	this->ANCPrivate = ANCPrivate;
}

FFIndexer *FFIndexer::CreateFFIndexer(const char *Filename, char *ErrorMsg, unsigned MsgSize) {
	AVFormatContext *FormatContext = NULL;

	if (av_open_input_file(&FormatContext, Filename, NULL, 0, NULL) != 0) {
		snprintf(ErrorMsg, MsgSize, "Can't open '%s'", Filename);
		return NULL;
	}

	// Do matroska indexing instead?
	if (!strcmp(FormatContext->iformat->name, "matroska")) {
		av_close_input_file(FormatContext);
		return new FFMatroskaIndexer(Filename, ErrorMsg, MsgSize);
	}

#ifdef HAALISOURCE
	// Do haali ts indexing instead?
	if (!strcmp(FormatContext->iformat->name, "mpeg") || !strcmp(FormatContext->iformat->name, "mpegts")) {
		av_close_input_file(FormatContext);
		return new FFHaaliIndexer(Filename, 0, ErrorMsg, MsgSize);
	}

	if (!strcmp(FormatContext->iformat->name, "ogg")) {
		av_close_input_file(FormatContext);
		return new FFHaaliIndexer(Filename, 1, ErrorMsg, MsgSize);
	}
#endif

	return new FFLAVFIndexer(Filename, FormatContext, ErrorMsg, MsgSize);
}

FFIndexer::FFIndexer() {
	DecodingBuffer = new int16_t[AVCODEC_MAX_AUDIO_FRAME_SIZE * 5];
}

FFIndexer::~FFIndexer() {
	delete[] DecodingBuffer;
}

bool FFIndexer::WriteAudio(SharedAudioContext &AudioContext, FFIndex *Index, int Track, int DBSize, char *ErrorMsg, unsigned MsgSize) {
	// Delay writer creation until after an audio frame has been decoded. This ensures that all parameters are known when writing the headers.
	if (DBSize > 0) {
		if (!AudioContext.W64W) {
			TAudioProperties AP;
			FillAP(AP, AudioContext.CTX, (*Index)[Track]);
			int FNSize = (*ANC)(SourceFile, Track, &AP, NULL, 0, ANCPrivate);
			char *WName = new char[FNSize];
			(*ANC)(SourceFile, Track, &AP, WName, FNSize, ANCPrivate);
			std::string WN(WName);
			delete[] WName;
			try {
				AudioContext.W64W = new Wave64Writer(WN.c_str(), av_get_bits_per_sample_format(AudioContext.CTX->sample_fmt),
					AudioContext.CTX->channels, AudioContext.CTX->sample_rate, AudioFMTIsFloat(AudioContext.CTX->sample_fmt));
			} catch (...) {
				snprintf(ErrorMsg, MsgSize, "Failed to write wave data");
				return false;
			}
		}

		AudioContext.W64W->WriteData(DecodingBuffer, DBSize);
	}

	return true;
}
