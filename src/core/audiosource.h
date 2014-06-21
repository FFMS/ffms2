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

#ifndef FFAUDIOSOURCE_H
#define FFAUDIOSOURCE_H

#include "utils.h"
#include "track.h"

#include <list>
#include <vector>

struct FFMS_AudioSource {
	struct AudioBlock {
		int64_t Age;
		int64_t Start;
		int64_t Samples;
		std::vector<uint8_t> Data;

		AudioBlock(int64_t Start)
		: Start(Start)
		, Samples(0)
		{
			static int64_t Now = 0;
			Age = Now++;
		}
	};
	typedef std::list<AudioBlock>::iterator CacheIterator;

	// delay in samples to apply to the audio
	int64_t Delay;
	// cache of decoded audio blocks
	std::list<AudioBlock> Cache;
	// max size of the cache in blocks
	size_t MaxCacheBlocks;
	// pointer to last element of the cache which should never be deleted
	CacheIterator CacheNoDelete;
	// bytes per sample * number of channels
	size_t BytesPerSample;

	bool NeedsResample;
	FFResampleContext ResampleContext;

	// Insert the current audio frame into the cache
	void CacheBlock(CacheIterator &pos);

	// Interleave the current audio frame and insert it into the cache
	void ResampleAndCache(CacheIterator pos);

	// Cache the unseekable beginning of the file once the output format is set
	void CacheBeginning();

	// Called after seeking
	virtual void Seek() { }
	// Read the next packet from the file
	virtual bool ReadPacket(AVPacket *) = 0;
	virtual void FreePacket(AVPacket *) { }

    // Close and reopen the source file to seek back to the beginning. Only
	// needs to do anything for formats that can't seek to the beginning otherwise.
	virtual void ReopenFile() { }
protected:
	// First sample which is stored in the decoding buffer
	int64_t CurrentSample;
	// Next packet to be read
	size_t PacketNumber;
	// Current audio frame
	const FrameInfo *CurrentFrame;
	// Track which this corresponds to
	int TrackNumber;
	// Number of packets which the demuxer requires to know where it is
	// If -1, seeking is assumed to be impossible
	int SeekOffset;

	// Buffer which audio is decoded into
	ScopedFrame DecodeFrame;
	FFMS_Track Frames;
	FFCodecContext CodecContext;
	FFMS_AudioProperties AP;

	void DecodeNextBlock(CacheIterator *cachePos = 0);
	// Initialization which has to be done after the codec is opened
	void Init(const FFMS_Index &Index, int DelayMode);

	FFMS_AudioSource(const char *SourceFile, FFMS_Index &Index, int Track);

public:
	virtual ~FFMS_AudioSource() { }
	FFMS_Track *GetTrack() { return &Frames; }
	const FFMS_AudioProperties& GetAudioProperties() const { return AP; }
	void GetAudio(void *Buf, int64_t Start, int64_t Count);

	FFMS_ResampleOptions *CreateResampleOptions() const;
	void SetOutputFormat(const FFMS_ResampleOptions *opt);
};

size_t GetSeekablePacketNumber(FFMS_Track const& Frames, size_t PacketNumber);

FFMS_AudioSource *CreateLavfAudioSource(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode);
FFMS_AudioSource *CreateMatroskaAudioSource(const char *SourceFile, int Track, FFMS_Index &Index, int DelayMode);
FFMS_AudioSource *CreateHaaliAudioSource(const char *SourceFile, int Track, FFMS_Index &Index, FFMS_Sources SourceMode, int DelayMode);

#endif
