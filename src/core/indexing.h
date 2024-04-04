//  Copyright (c) 2007-2017 Fredrik Mellbin
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

#ifndef INDEXING_H
#define INDEXING_H

#include "utils.h"

#include <set>
#include <map>
#include <memory>
#include <atomic>

extern "C" {
#include <libavutil/avutil.h>
}

class Wave64Writer;
class ZipFile;

struct SharedAVContext {
    AVCodecContext *CodecContext = nullptr;
    AVCodecParserContext *Parser = nullptr;
    int64_t CurrentSample = 0;
    ~SharedAVContext();
};

struct FFMS_Index : public std::vector<FFMS_Track> {
    FFMS_Index(FFMS_Index const&) = delete;
    FFMS_Index& operator=(FFMS_Index const&) = delete;
    void ReadIndex(ZipFile &zf, const char* IndexFile);
    void WriteIndex(ZipFile &zf);
public:
    static void CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]);

    int ErrorHandling;
    int64_t Filesize;
    uint8_t Digest[20];
    std::map<std::string, std::string> LAVFOpts;

    void Finalize(std::vector<SharedAVContext> const& video_contexts, const char *Format);
    bool CompareFileSignature(const char *Filename);
    void WriteIndexFile(const char *IndexFile);
    uint8_t *WriteIndexBuffer(size_t *Size);

    FFMS_Index(const char *IndexFile);
    FFMS_Index(const uint8_t *Buffer, size_t Size);
    FFMS_Index(int64_t Filesize, uint8_t Digest[20], int ErrorHandling, const std::map<std::string, std::string> &LAVFOpts);
};

struct FFMS_Indexer {
private:
    std::map<int, FFMS_AudioProperties> LastAudioProperties;
    FFMS_Indexer(FFMS_Indexer const&) = delete;
    FFMS_Indexer& operator=(FFMS_Indexer const&) = delete;
    AVFormatContext *FormatContext = nullptr;
    std::set<int> IndexMask;
    std::map<std::string, std::string> LAVFOpts;
    int ErrorHandling = FFMS_IEH_CLEAR_TRACK;
    TIndexCallback IC = nullptr;
    void *ICPrivate = nullptr;
    std::string SourceFile;
    AVFrame *DecodeFrame = nullptr;

    int64_t Filesize;
    uint8_t Digest[20];

    void ReadTS(const AVPacket *Packet, int64_t &TS, bool &UseDTS);
    void CheckAudioProperties(int Track, AVCodecContext *Context);
    uint32_t IndexAudioPacket(int Track, AVPacket *Packet, SharedAVContext &Context, FFMS_Index &TrackIndices);
    void ParseVideoPacket(SharedAVContext &VideoContext, AVPacket *pkt, int *RepeatPict, int *FrameType, bool *Invisible, bool *SecondField, enum AVPictureStructure *LastPicStruct);
    void Free();
public:
    FFMS_Indexer(const char *Filename, const FFMS_KeyValuePair *DemuxerOptions, int NumOptions);
    ~FFMS_Indexer();

    void SetIndexTrack(int Track, bool Index);
    void SetIndexTrackType(int TrackType, bool Index);
    void SetErrorHandling(int ErrorHandling_);
    void SetProgressCallback(TIndexCallback IC_, void *ICPrivate_);

    FFMS_Index *DoIndexing();
    int GetNumberOfTracks();
    FFMS_TrackType GetTrackType(int Track);
    const char *GetTrackCodec(int Track);
    const char *GetFormatName();
};


#endif
