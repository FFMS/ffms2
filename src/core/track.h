//  Copyright (c) 2013 Thomas Goyne <tgoyne@gmail.com>
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

#ifndef TRACK_H
#define TRACK_H

#include "ffms.h"

#include <cstddef>
#include <vector>
#include <memory>

class ZipFile;

struct FrameInfo {
    int64_t PTS;
    int64_t OriginalPTS;
    int64_t FilePos;
    int64_t SampleStart;
    uint32_t SampleCount;
    size_t OriginalPos;         // Frames[Frames[n].OriginalPos] will be the n-th frame in decoding order
    size_t PosInDecodingOrder;
    int FrameType;
    int RepeatPict;
    bool KeyFrame;
    bool MarkedHidden;
    bool SecondField;

    // If true, no frame corresponding to this packet will be output
    constexpr bool Skipped() const { return MarkedHidden || SecondField; }
};

struct FFMS_Track {
private:
    typedef std::vector<FrameInfo> frame_vec;
    struct TrackData {
        frame_vec Frames;
        std::vector<int> RealFrameNumbers;
        std::vector<FFMS_FrameInfo> PublicFrameInfo;
    };

    std::shared_ptr<TrackData> Data;

    void MaybeReorderFrames();
    void GeneratePublicInfo();

public:
    FFMS_TrackType TT = FFMS_TYPE_UNKNOWN;
    FFMS_TrackTimeBase TB = FFMS_TrackTimeBase{ 0, 0 };
    int MaxBFrames = 0;
    bool UseDTS = false;
    bool HasTS = false;
    bool HasDiscontTS = false;
    int64_t LastDuration = 0;
    int SampleRate = 0; // not persisted

    void AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos = 0, bool Invisible = false, bool SecondField = false);
    void AddAudioFrame(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, bool KeyFrame, int64_t FilePos = 0, bool Invisible = false);

    void MaybeHideFrames();
    void FinalizeTrack();
    void FillAudioGaps();

    int FindClosestVideoKeyFrame(int Frame) const;
    int FrameFromPTS(int64_t PTS, bool AllowHidden = false) const;
    int FrameFromPos(int64_t Pos, bool AllowHidden = false) const;
    int ClosestFrameFromPTS(int64_t PTS) const;
    int RealFrameNumber(int Frame) const;
    int VisibleFrameCount() const;

    const FFMS_FrameInfo *GetFrameInfo(size_t N) const;

    void WriteTimecodes(const char *TimecodeFile) const;
    void Write(ZipFile &Stream) const;

    typedef frame_vec::allocator_type allocator_type;
    typedef frame_vec::size_type size_type;
    typedef frame_vec::difference_type difference_type;
    typedef frame_vec::const_pointer pointer;
    typedef frame_vec::const_reference reference;
    typedef frame_vec::value_type value_type;
    typedef frame_vec::const_iterator iterator;
    typedef frame_vec::const_reverse_iterator reverse_iterator;

    void clear() {
        Data = std::make_shared<TrackData>();
    }

    bool empty() const { return Data->Frames.empty(); }
    size_type size() const { return Data->Frames.size(); }
    reference operator[](size_type pos) const { return Data->Frames[pos]; }
    reference front() const { return Data->Frames.front(); }
    reference back() const { return Data->Frames.back(); }
    iterator begin() const { return Data->Frames.begin(); }
    iterator end() const { return Data->Frames.end(); }

    FFMS_Track();
    FFMS_Track(ZipFile &Stream);
    FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool HasDiscontTS, bool UseDTS, bool HasTS = true);
};

#endif
