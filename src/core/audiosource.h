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
#include <atomic>

struct FFMS_AudioSource {
    struct AudioBlock {
        struct Free {
            void operator()(uint8_t *ptr) const {
                free(ptr);
            }
        };

        int64_t Age;
        int64_t Start;
        int64_t Samples = 0;
        size_t DataSize = 0;
        std::unique_ptr<uint8_t, Free> Data;

        AudioBlock(int64_t Start)
            : Start(Start) {
            static std::atomic<int64_t> Now{ 0 };
            Age = Now++;
        }

        uint8_t *Grow(size_t size) {
            auto ptr = static_cast<uint8_t *>(realloc(Data.get(), DataSize + size));
            if (!ptr)
                throw std::bad_alloc();
            Data.release();
            Data.reset(ptr);
            ptr += DataSize;
            DataSize += size;
            return ptr;
        }
    };
    typedef std::list<AudioBlock>::iterator CacheIterator;

    AVFormatContext *FormatContext = nullptr;
    int64_t LastValidTS;
    std::string SourceFile;

    // delay in samples to apply to the audio
    int64_t Delay = 0;
    // cache of decoded audio blocks
    std::list<AudioBlock> Cache;
    // max size of the cache in blocks
    size_t MaxCacheBlocks = 50;
    // pointer to last element of the cache which should never be deleted
    CacheIterator CacheNoDelete;
    // bytes per sample * number of channels, *after* resampling if applicable
    size_t BytesPerSample = 0;

    bool NeedsResample = false;

    struct SwrFreeWrapper {
        void operator()(SwrContext *c) const {
            swr_free(&c);
        }
    };

    typedef std::unique_ptr<SwrContext, SwrFreeWrapper> FFResampleContext;
    FFResampleContext ResampleContext;

    // Insert the current audio frame into the cache
    AudioBlock *CacheBlock(CacheIterator &pos);

    // Interleave the current audio frame and insert it into the cache
    void ResampleAndCache(CacheIterator pos);

    // Cache the unseekable beginning of the file once the output format is set
    void CacheBeginning();

    // Called after seeking
    void Seek();
    // Read the next packet from the file
    bool ReadPacket(AVPacket *);

    // Close and reopen the source file to seek back to the beginning. Only
    // needs to do anything for formats that can't seek to the beginning otherwise.
    //
    // If the file is not already open, it is merely just opened.
    void OpenFile();

    // First sample which is stored in the decoding buffer
    int64_t CurrentSample = -1;
    // Next packet to be read
    size_t PacketNumber = 0;
    // Current audio frame
    const FrameInfo *CurrentFrame = nullptr;
    // Track which this corresponds to
    int TrackNumber;
    // Number of packets which the demuxer requires to know where it is
    // If -1, seeking is assumed to be impossible
    int SeekOffset = 0;

    // Buffer which audio is decoded into
    AVFrame *DecodeFrame = nullptr;
    FFMS_Track Frames;
    AVCodecContext *CodecContext = nullptr;
    FFMS_AudioProperties AP;

    int DecodeNextBlock(CacheIterator *cachePos = 0);
    // Initialization which has to be done after the codec is opened
    void Init(const FFMS_Index &Index, int DelayMode);

    int64_t FrameTS(size_t Packet) const;

    void Free();
public:
    FFMS_AudioSource(const char *SourceFile, FFMS_Index &Index, int Track, int DelayMode);
    ~FFMS_AudioSource();
    FFMS_Track *GetTrack() { return &Frames; }
    const FFMS_AudioProperties& GetAudioProperties() const { return AP; }
    void GetAudio(void *Buf, int64_t Start, int64_t Count);

    std::unique_ptr<FFMS_ResampleOptions> CreateResampleOptions() const;
    void SetOutputFormat(FFMS_ResampleOptions const& opt);

    static size_t GetSeekablePacketNumber(FFMS_Track const& Frames, size_t PacketNumber);
};

#endif
