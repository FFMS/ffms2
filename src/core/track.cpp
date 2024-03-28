//  Copyright (c) 2014 Thomas Goyne <tgoyne@gmail.com>
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

#include "track.h"

#include "utils.h"
#include "zipfile.h"
#include "indexing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/common.h>
#include <libavutil/mathematics.h>
}

namespace {
FrameInfo ReadFrame(ZipFile &stream, FrameInfo const& prev, const FFMS_TrackType TT) {
    FrameInfo f{};
    f.PTS = stream.Read<int64_t>() + prev.PTS;
    f.OriginalPTS = stream.Read<int64_t>() + prev.OriginalPTS;
    f.KeyFrame = !!stream.Read<int8_t>();
    f.FilePos = stream.Read<int64_t>() + prev.FilePos;
    f.Hidden = !!stream.Read<int8_t>();

    if (TT == FFMS_TYPE_AUDIO) {
        f.SampleStart = prev.SampleStart + prev.SampleCount;
        f.SampleCount = stream.Read<uint32_t>() + prev.SampleCount;
    } else if (TT == FFMS_TYPE_VIDEO) {
        f.OriginalPos = static_cast<size_t>(stream.Read<uint64_t>() + prev.OriginalPos + 1);
        f.RepeatPict = stream.Read<int32_t>();
    }
    return f;
}

static void WriteFrame(ZipFile &stream, FrameInfo const& f, FrameInfo const& prev, const FFMS_TrackType TT) {
    stream.Write(f.PTS - prev.PTS);
    stream.Write(f.OriginalPTS - prev.OriginalPTS);
    stream.Write<int8_t>(f.KeyFrame);
    stream.Write(f.FilePos - prev.FilePos);
    stream.Write<uint8_t>(f.Hidden);

    if (TT == FFMS_TYPE_AUDIO)
        stream.Write(f.SampleCount - prev.SampleCount);
    else if (TT == FFMS_TYPE_VIDEO) {
        stream.Write(static_cast<uint64_t>(f.OriginalPos) - prev.OriginalPos - 1);
        stream.Write<int32_t>(f.RepeatPict);
    }
}
}

FFMS_Track::FFMS_Track()
    : Data(std::make_shared<TrackData>())
{
}

FFMS_Track::FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool HasDiscontTS, bool UseDTS, bool HasTS)
    : Data(std::make_shared<TrackData>())
    , TT(TT)
    , UseDTS(UseDTS)
    , HasTS(HasTS)
    , HasDiscontTS(HasDiscontTS) {
    TB.Num = Num;
    TB.Den = Den;
}

FFMS_Track::FFMS_Track(ZipFile &stream)
    : Data(std::make_shared<TrackData>()) {
    frame_vec &Frames = Data->Frames;
    TT = static_cast<FFMS_TrackType>(stream.Read<uint8_t>());
    TB.Num = stream.Read<int64_t>();
    TB.Den = stream.Read<int64_t>();
    LastDuration = stream.Read<int64_t>();
    MaxBFrames = stream.Read<int32_t>();
    UseDTS = !!stream.Read<uint8_t>();
    HasTS = !!stream.Read<uint8_t>();
    HasDiscontTS = !!stream.Read<uint8_t>();
    size_t FrameCount = static_cast<size_t>(stream.Read<uint64_t>());

    if (!FrameCount) return;

    FrameInfo temp{};
    Frames.reserve(FrameCount);
    for (size_t i = 0; i < FrameCount; ++i)
        Frames.push_back(ReadFrame(stream, i == 0 ? temp : Frames.back(), TT));

    if (TT == FFMS_TYPE_VIDEO) {
        GeneratePublicInfo();

        // PosInDecodingOrder is currently not stored in the index for backwards compatibility, so
        // derive it here (the asserts in FinalizeTrack guarantee that this matches the original values).
        // FIXME store OriginalPos in the index the next time the format changes
        for (size_t i = 0; i < Frames.size(); i++) {
            Frames[Frames[i].OriginalPos].PosInDecodingOrder = i;
        }
    }
}

void FFMS_Track::Write(ZipFile &stream) const {
    frame_vec &Frames = Data->Frames;
    stream.Write<uint8_t>(TT);
    stream.Write(TB.Num);
    stream.Write(TB.Den);
    stream.Write<int64_t>(LastDuration);
    stream.Write<int32_t>(MaxBFrames);
    stream.Write<uint8_t>(UseDTS);
    stream.Write<uint8_t>(HasTS);
    stream.Write<uint8_t>(HasDiscontTS);
    stream.Write<uint64_t>(size());

    if (empty()) return;

    FrameInfo temp{};
    for (size_t i = 0; i < size(); ++i)
        WriteFrame(stream, Frames[i], i == 0 ? temp : Frames[i - 1], TT);
}

void FFMS_Track::AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, bool Hidden) {
    Data->Frames.push_back({ PTS, 0, FilePos, 0, 0, 0, 0, FrameType, RepeatPict, KeyFrame, Hidden });
}

void FFMS_Track::AddAudioFrame(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, bool KeyFrame, int64_t FilePos, bool Hidden) {
    if (SampleCount > 0) {
        Data->Frames.push_back({ PTS, 0, FilePos, SampleStart, SampleCount,
            0, 0, 0, 0, KeyFrame, Hidden });
    }
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) const {
    frame_vec &Frames = Data->Frames;
    FileHandle file(TimecodeFile, "w", FFMS_ERROR_TRACK, FFMS_ERROR_FILE_WRITE);

    file.Printf("# timecode format v2\n");
    for (size_t i = 0; i < size(); ++i) {
        if (!Frames[i].Hidden)
            file.Printf("%.02f\n", (Frames[i].PTS * TB.Num) / (double)TB.Den);
    }
}

static bool PTSComparison(FrameInfo FI1, FrameInfo FI2) {
    return FI1.PTS < FI2.PTS;
}

int FFMS_Track::FrameFromPTS(int64_t PTS) const {
    FrameInfo F;
    F.PTS = PTS;

    auto Pos = std::lower_bound(begin(), end(), F, PTSComparison);
    while (Pos != end() && Pos->Hidden && Pos->PTS == PTS)
        Pos++;

    if (Pos == end() || Pos->PTS != PTS)
        return -1;
    return std::distance(begin(), Pos);
}

int FFMS_Track::FrameFromPos(int64_t Pos) const {
    for (size_t i = 0; i < size(); i++)
        if (Data->Frames[i].FilePos == Pos && !Data->Frames[i].Hidden)
            return static_cast<int>(i);
    return -1;
}

int FFMS_Track::ClosestFrameFromPTS(int64_t PTS) const {
    FrameInfo F;
    F.PTS = PTS;

    auto Pos = std::lower_bound(begin(), end(), F, PTSComparison);
    while (Pos != end() && Pos->Hidden && Pos->PTS == PTS)
        Pos++;

    if (Pos == end())
        return static_cast<int>(size() - 1);
    size_t Frame = std::distance(begin(), Pos);
    if (Pos == begin() || FFABS(Pos->PTS - PTS) <= FFABS((Pos - 1)->PTS - PTS))
        return static_cast<int>(Frame);
    return static_cast<int>(Frame - 1);
}

int FFMS_Track::FindClosestVideoKeyFrame(int Frame) const {
    frame_vec &Frames = Data->Frames;
    Frame = std::min(std::max(Frame, 0), static_cast<int>(size()) - 1);
    size_t PosInDecodingOrder = Frames[Frame].PosInDecodingOrder;
    for (; PosInDecodingOrder > 0 && !(Frames[Frames[PosInDecodingOrder].OriginalPos].KeyFrame && Frames[Frames[PosInDecodingOrder].OriginalPos].PTS <= Frames[Frame].PTS); PosInDecodingOrder--);

    return Frames[PosInDecodingOrder].OriginalPos;
}

int FFMS_Track::RealFrameNumber(int Frame) const {
    return Data->RealFrameNumbers[Frame];
}

int FFMS_Track::VisibleFrameCount() const {
    return TT == FFMS_TYPE_AUDIO ? static_cast<int>(Data->Frames.size()) : static_cast<int>(Data->RealFrameNumbers.size());
}

void FFMS_Track::MaybeReorderFrames() {
    frame_vec &Frames = Data->Frames;
    // First check if we need to do anything
    bool has_b_frames = false;
    for (size_t i = 1; i < size(); ++i) {
        // If the timestamps are already out of order, then they actually are
        // presentation timestamps and we don't need to do anything
        if (Frames[i].PTS < Frames[i - 1].PTS)
            return;

        if (Frames[i].FrameType == AV_PICTURE_TYPE_B) {
            has_b_frames = true;

            // Reordering files with multiple b-frames is currently not
            // supported
            if (Frames[i - 1].FrameType == AV_PICTURE_TYPE_B)
                return;
        }
    }

    // Don't need to do anything if there are no b-frames as presentation order
    // equals decoding order
    if (!has_b_frames)
        return;

    // We have b-frames, but the timestamps are monotonically increasing. This
    // means that the timestamps we have are decoding timestamps, and we want
    // presentation time stamps. Convert DTS to PTS by swapping the timestamp
    // of each b-frame with the frame before it. This only works for the
    // specific case of b-frames which reference the frame immediately after
    // them temporally, but that happens to cover the only files I've seen
    // with b-frames and no presentation timestamps.
    for (size_t i = 1; i < size(); ++i) {
        if (Frames[i].FrameType == AV_PICTURE_TYPE_B)
            std::swap(Frames[i].PTS, Frames[i - 1].PTS);
    }
}

void FFMS_Track::MaybeHideFrames() {
    frame_vec &Frames = Data->Frames;
    // Awful handling for interlaced H.264: each frame is output twice, so hide
    // frames with an invalid file position. The PTS will not match sometimes,
    // since libavformat makes up timestamps... but only sometimes.
    for (size_t i = 1; i < size(); ++i) {
        FrameInfo const& prev = Frames[i - 1];
        FrameInfo& cur = Frames[i];

        if (prev.FilePos >= 0 && (cur.FilePos == -1 || cur.FilePos == prev.FilePos) && !prev.Hidden)
            cur.Hidden = true;
    }
}

void FFMS_Track::FillAudioGaps() {
    frame_vec &Frames = Data->Frames;
    // There may not be audio data for the entire duration of the audio track,
    // as some formats support gaps between the end time of one packet and the
    // PTS of the next audio packet, and we should zero-fill those gaps.
    // However, garbage or missing timestamps for audio tracks are very common,
    // so we only want to trust them if they're all present, monotonically
    // increasing, and result in a total duration meaningfully longer than the
    // samples we have would cover.
    if (size() < 2 || !HasTS || front().PTS == AV_NOPTS_VALUE || back().PTS == AV_NOPTS_VALUE)
        return;

    const auto DurationToSamples = [this](int64_t Dur) {
        auto Num = TB.Num * SampleRate;
        auto Den = TB.Den * 1000;
        return av_rescale(Dur, Num, Den);
    };

    const auto SamplesToDuration = [this](int64_t Samples) {
        auto Num = TB.Den * 1000;
        auto Den = TB.Num * SampleRate;
        return av_rescale(Samples, Num, Den);
    };

    if (HasDiscontTS) {
        int64_t shift = 0;
        Frames[0].OriginalPTS = Frames[0].PTS;
        for (size_t i = 1; i < size(); i++) {
            Frames[i].OriginalPTS = Frames[i].PTS;
            if (Frames[i].PTS != AV_NOPTS_VALUE && Frames[i].OriginalPTS <= Frames[i-1].OriginalPTS)
                shift = -(Frames[i].PTS) + Frames[i-1].PTS + SamplesToDuration(Frames[i-1].SampleCount);
            Frames[i].PTS += shift;
        }
    }

    const auto ActualSamples = back().SampleStart + back().SampleCount;
    const auto ExpectedSamples = DurationToSamples(back().PTS - front().PTS) + back().SampleCount;
    if (ActualSamples + 5 > ExpectedSamples) // arbitrary threshold to cover rounding/not worth adjusting
        return;

    // Verify that every frame has a timestamp and that they monotonically
    // increase, as otherwise we can't trust them
    auto PrevPTS = front().PTS - 1;
    for (auto const& frame : *this) {
        if (frame.PTS == AV_NOPTS_VALUE || frame.PTS <= PrevPTS)
            return;
        PrevPTS = frame.PTS;
    }

    // There are some missing samples and the timestamps appear to all be valid,
    // so go ahead and extend the frames to cover the gaps
    const auto FirstPTS = front().PTS;
    auto PrevFrame = &Frames.front();
    int32_t Shift = 0;
    for (auto& Frame : Frames) {
        if (Shift > 0)
            Frame.SampleStart += Shift;

        const auto ExpectedStartSample = DurationToSamples(Frame.PTS - FirstPTS);
        const auto Gap = static_cast<int32_t>(ExpectedStartSample - Frame.SampleStart);
        if (Gap > 0) {
            PrevFrame->SampleCount += Gap;
            Frame.SampleStart = ExpectedStartSample;
        }
        Shift += Gap;
        PrevFrame = &Frame;
    }
}

void FFMS_Track::FinalizeTrack() {
    frame_vec &Frames = Data->Frames;
    // With some formats (such as Vorbis) a bad final packet results in a
    // frame with PTS 0, which we don't want to sort to the beginning
    if (size() > 2 && front().PTS >= back().PTS)
        Frames.pop_back();

    if (TT != FFMS_TYPE_VIDEO)
        return;

    for (size_t i = 0; i < size(); i++) {
        Frames[i].PosInDecodingOrder = i;
        Frames[i].OriginalPTS = Frames[i].PTS;
    }

    MaybeReorderFrames();

    if (size() > 2 && HasDiscontTS) {
        std::vector<size_t> secs = { 0 };

        auto lastPTS = Frames[0].PTS;
        const auto thresh = std::abs(Frames[1].PTS - Frames[0].PTS) * 16; // A bad approximation of 16 frames, the max reorder buffer size.
        for (size_t i = 0; i < size(); i++) {
            if (Frames[i].PTS < lastPTS && (lastPTS - Frames[i].PTS) > thresh && i + 1 < size()) {
                secs.push_back(i);
                i++; // Sections must be at least 2 frames long.
            }
            lastPTS = Frames[i].PTS;
        }

        // We need to sort each distinct sections by PTS to account for any reordering.
        for (size_t i = 0; i < secs.size() - 1; i++)
            sort(Frames.begin() + secs[i], Frames.begin() + secs[i + 1], PTSComparison);
        sort(Frames.begin() + secs.back(), Frames.end(), PTSComparison);

        // Try and make up some sane timestamps based on previous sections, while
        // keeping the same frame durations.
        for (size_t i = 1; i < secs.size(); i++) {
            const auto shift = -(Frames[secs[i]].PTS) + (Frames[secs[i] + 1].PTS - Frames[secs[i]].PTS) + Frames[secs[i] - 1].PTS;
            size_t end;
            if (i == secs.size() - 1)
                end = Frames.size();
            else
                end = secs[i + 1];
            for (size_t j = secs[i]; j < end; j++)
                Frames[j].PTS += shift;
        }
    } else {
        sort(Frames.begin(), Frames.end(), PTSComparison);
    }

    std::vector<size_t> ReorderTemp;
    ReorderTemp.reserve(size());

    for (size_t i = 0; i < size(); i++)
        ReorderTemp.push_back(Frames[i].PosInDecodingOrder);

    for (size_t i = 0; i < size(); i++)
        Frames[ReorderTemp[i]].OriginalPos = i;

    for (size_t i = 0; i < size(); i++) {
        if (Frames[Frames[i].OriginalPos].PosInDecodingOrder != i ||
            Frames[Frames[i].PosInDecodingOrder].OriginalPos != i)
            throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_CODEC, "Insanity detected when tracking frame reordering");
    }

    GeneratePublicInfo();

    // If the last packet in the file did not have a duration set,
    // fudge one based on the previous frame's duration.
    if (LastDuration == 0) {
        size_t InfoSize = Data->PublicFrameInfo.size();
        if (InfoSize >= 2)
            LastDuration = Data->PublicFrameInfo[InfoSize - 1].PTS - Data->PublicFrameInfo[InfoSize - 2].PTS;
    }
}

void FFMS_Track::GeneratePublicInfo() {
    frame_vec &Frames = Data->Frames;
    std::vector<int> &RealFrameNumbers = Data->RealFrameNumbers;
    std::vector<FFMS_FrameInfo> &PublicFrameInfo = Data->PublicFrameInfo;
    RealFrameNumbers.reserve(size());
    PublicFrameInfo.reserve(size());
    for (size_t i = 0; i < size(); ++i) {
        if (Frames[i].Hidden)
            continue;
        RealFrameNumbers.push_back(static_cast<int>(i));

        FFMS_FrameInfo info = { Frames[i].PTS, Frames[i].RepeatPict, Frames[i].KeyFrame, Frames[i].OriginalPTS };
        PublicFrameInfo.push_back(info);
    }
}

const FFMS_FrameInfo *FFMS_Track::GetFrameInfo(size_t N) const {
    std::vector<FFMS_FrameInfo> &PublicFrameInfo = Data->PublicFrameInfo;
    if (N >= PublicFrameInfo.size()) return nullptr;
    return &PublicFrameInfo[N];
}
