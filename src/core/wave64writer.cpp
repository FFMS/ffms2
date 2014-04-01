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

#include "wave64writer.h"
#include <cstring>

#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_PCM 1

static const uint8_t GuidRIFF[16]={
	// {66666972-912E-11CF-A5D6-28DB04C10000}
	0x72, 0x69, 0x66, 0x66, 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00
};

static const uint8_t GuidWAVE[16]={
	// {65766177-ACF3-11D3-8CD1-00C04F8EDB8A}
	0x77, 0x61, 0x76, 0x65, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

static const uint8_t Guidfmt[16]={
	// {20746D66-ACF3-11D3-8CD1-00C04F8EDB8A}
	0x66, 0x6D, 0x74, 0x20, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

static const uint8_t Guiddata[16]={
	// {61746164-ACF3-11D3-8CD1-00C04F8EDB8A}
	0x64, 0x61, 0x74, 0x61, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

Wave64Writer::Wave64Writer(const char *Filename, uint16_t BytesPerSample, uint16_t Channels, uint32_t SamplesPerSec, bool IsFloat)
: WavFile(Filename, std::ios::out | std::ios::binary | std::ios::trunc)
, BytesWritten(0)
, SamplesPerSec(SamplesPerSec)
, BytesPerSample(BytesPerSample)
, Channels(Channels)
, IsFloat(IsFloat)
{
	if (!WavFile.is_open())
		throw "Failed to open destination file for writing";

	WriteHeader(true, IsFloat);
}

Wave64Writer::~Wave64Writer() {
	WriteHeader(false, IsFloat);
}

void Wave64Writer::WriteHeader(bool Initial, bool IsFloat) {
	FFMS_WAVEFORMATEX WFEX;
	if (IsFloat)
		WFEX.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	else
		WFEX.wFormatTag = WAVE_FORMAT_PCM;
	WFEX.nChannels = Channels;
	WFEX.nSamplesPerSec = SamplesPerSec;
	WFEX.nAvgBytesPerSec = BytesPerSample * Channels * SamplesPerSec;
	WFEX.nBlockAlign = BytesPerSample * Channels;
	WFEX.wBitsPerSample = BytesPerSample * 8;
	WFEX.cbSize = 0;

	uint64_t Header[14];

	memset(Header, 0, sizeof(Header));

	memcpy(Header + 0, GuidRIFF, 16);
	if (Initial) {
		Header[2] = 0x7F00000000000000ull;
	} else {
		Header[2] = BytesWritten + sizeof(Header);
	}

	memcpy(Header + 3, GuidWAVE, 16);
	memcpy(Header + 5, Guidfmt, 16);

	Header[7] = 48;

	memcpy(Header + 8, &WFEX, sizeof(WFEX));
	memcpy(Header + 11, Guiddata, 16);

	if (Initial)
		Header[13] = 0x7E00000000000000ull;
	else
		Header[13] = BytesWritten + 24;

	std::streampos CPos = WavFile.tellp();
	WavFile.seekp(0, std::ios::beg);
	WavFile.write(reinterpret_cast<const char *>(Header), sizeof(Header));
	if (!Initial)
		WavFile.seekp(CPos, std::ios::beg);
}

void Wave64Writer::WriteData(AVFrame const& Frame) {
	uint64_t Length = (uint64_t) Frame.nb_samples * BytesPerSample * Channels;
	if (Channels > 1 && av_sample_fmt_is_planar(static_cast<AVSampleFormat>(Frame.format))) {
		for (int32_t sample = 0; sample < Frame.nb_samples; ++sample) {
			for (int32_t channel = 0; channel < Channels; ++channel)
				WavFile.write(reinterpret_cast<char *>(&Frame.extended_data[channel][sample * BytesPerSample]), BytesPerSample);
		}
	}
	else {
		WavFile.write(reinterpret_cast<char *>(Frame.extended_data[0]), Length);
	}
	BytesWritten += Length;
}
