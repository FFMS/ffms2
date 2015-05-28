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

#include "zipfile.h"

#include <algorithm>

#include <libavutil/avutil.h>
#include <libavutil/common.h>

namespace {
FrameInfo ReadFrame(ZipFile &stream, FrameInfo const& prev, const FFMS_TrackType TT) {
	FrameInfo f{};
	f.PTS = stream.Read<int64_t>() + prev.PTS;
	f.KeyFrame = !!stream.Read<int8_t>();
	f.FilePos = stream.Read<int64_t>() + prev.FilePos;

	if (TT == FFMS_TYPE_AUDIO) {
		f.SampleStart = prev.SampleStart + prev.SampleCount;
		f.SampleCount = stream.Read<uint32_t>() + prev.SampleCount;
	}
	else if (TT == FFMS_TYPE_VIDEO) {
		f.OriginalPos = static_cast<size_t>(stream.Read<uint64_t>() + prev.OriginalPos + 1);
		f.RepeatPict = stream.Read<int32_t>();
		f.Hidden = !!stream.Read<uint8_t>();
	}
	return f;
}

static void WriteFrame(ZipFile &stream, FrameInfo const& f, FrameInfo const& prev, const FFMS_TrackType TT) {
	stream.Write(f.PTS - prev.PTS);
	stream.Write<int8_t>(f.KeyFrame);
	stream.Write(f.FilePos - prev.FilePos);

	if (TT == FFMS_TYPE_AUDIO)
		stream.Write(f.SampleCount - prev.SampleCount);
	else if (TT == FFMS_TYPE_VIDEO) {
		stream.Write(static_cast<uint64_t>(f.OriginalPos) - prev.OriginalPos - 1);
		stream.Write<int32_t>(f.RepeatPict);
		stream.Write<uint8_t>(f.Hidden);
	}
}
}

FFMS_Track::FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool UseDTS, bool HasTS)
: TT(TT)
, UseDTS(UseDTS)
, HasTS(HasTS)
{
	this->TB.Num = Num;
	this->TB.Den = Den;
}

FFMS_Track::FFMS_Track(ZipFile &stream) {
	TT = static_cast<FFMS_TrackType>(stream.Read<uint8_t>());
	TB.Num = stream.Read<int64_t>();
	TB.Den = stream.Read<int64_t>();
	MaxBFrames = stream.Read<int32_t>();
	UseDTS = !!stream.Read<uint8_t>();
	HasTS = !!stream.Read<uint8_t>();
	size_t FrameCount = static_cast<size_t>(stream.Read<uint64_t>());

	if (!FrameCount) return;

	FrameInfo temp{};
	Frames.reserve(FrameCount);
	for (size_t i = 0; i < FrameCount; ++i)
		Frames.push_back(ReadFrame(stream, i == 0 ? temp : Frames.back(), TT));

	if (TT == FFMS_TYPE_VIDEO)
		GeneratePublicInfo();
}

void FFMS_Track::Write(ZipFile &stream) const {
	stream.Write<uint8_t>(TT);
	stream.Write(TB.Num);
	stream.Write(TB.Den);
	stream.Write<int32_t>(MaxBFrames);
	stream.Write<uint8_t>(UseDTS);
	stream.Write<uint8_t>(HasTS);
	stream.Write<uint64_t>(size());

	if (empty()) return;

	FrameInfo temp{};
	for (size_t i = 0; i < size(); ++i)
		WriteFrame(stream, Frames[i], i == 0 ? temp : Frames[i - 1], TT);
}

void FFMS_Track::AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, bool Hidden) {
	Frames.push_back({PTS, FilePos, 0, 0, 0, FrameType, RepeatPict, KeyFrame, Hidden});
}

void FFMS_Track::AddAudioFrame(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, bool KeyFrame, int64_t FilePos) {
	if (SampleCount > 0) {
		Frames.push_back({PTS, FilePos, SampleStart, SampleCount,
			0, 0, 0, KeyFrame, false});
	}
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) const {
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
	if (Pos == end() || Pos->PTS != PTS)
		return -1;
	return std::distance(begin(), Pos);
}

int FFMS_Track::FrameFromPos(int64_t Pos) const {
	for (size_t i = 0; i < size(); i++)
	if (Frames[i].FilePos == Pos)
        return static_cast<int>(i);
	return -1;
}

int FFMS_Track::ClosestFrameFromPTS(int64_t PTS) const {
	FrameInfo F;
	F.PTS = PTS;

	auto Pos = std::lower_bound(begin(), end(), F, PTSComparison);
	if (Pos == end())
        return static_cast<int>(size() - 1);
	size_t Frame = std::distance(begin(), Pos);
	if (Pos == begin() || FFABS(Pos->PTS - PTS) <= FFABS((Pos - 1)->PTS - PTS))
        return static_cast<int>(Frame);
    return static_cast<int>(Frame - 1);
}

int FFMS_Track::FindClosestVideoKeyFrame(int Frame) const {
	Frame = std::min(std::max(Frame, 0), static_cast<int>(size()) - 1);
	for (; Frame > 0 && !Frames[Frame].KeyFrame; Frame--);
	for (; Frame > 0 && !Frames[Frames[Frame].OriginalPos].KeyFrame; Frame--);
	return Frame;
}

int FFMS_Track::RealFrameNumber(int Frame) const {
	return RealFrameNumbers[Frame];
}

int FFMS_Track::VisibleFrameCount() const {
    return TT == FFMS_TYPE_AUDIO ? static_cast<int>(Frames.size()) : static_cast<int>(RealFrameNumbers.size());
}

void FFMS_Track::MaybeReorderFrames() {
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
	// Awful handling for interlaced H.264: each frame is output twice, so hide
	// frames with an invalid file position and PTS equal to the previous one
	for (size_t i = 1; i < size(); ++i) {
		FrameInfo const& prev = Frames[i - 1];
		FrameInfo& cur = Frames[i];

		if (prev.FilePos >= 0 && (cur.FilePos == -1 || cur.FilePos == prev.FilePos) && cur.PTS == prev.PTS)
			cur.Hidden = true;
	}
}

void FFMS_Track::FinalizeTrack() {
	// With some formats (such as Vorbis) a bad final packet results in a
	// frame with PTS 0, which we don't want to sort to the beginning
	if (size() > 2 && front().PTS >= back().PTS)
		Frames.pop_back();

	if (TT != FFMS_TYPE_VIDEO)
		return;

	for (size_t i = 0; i < size(); i++)
		Frames[i].OriginalPos = i;

	MaybeReorderFrames();
	MaybeHideFrames();

	sort(Frames.begin(), Frames.end(), PTSComparison);

	std::vector<size_t> ReorderTemp;
	ReorderTemp.reserve(size());

	for (size_t i = 0; i < size(); i++)
		ReorderTemp.push_back(Frames[i].OriginalPos);

	for (size_t i = 0; i < size(); i++)
		Frames[ReorderTemp[i]].OriginalPos = i;

	GeneratePublicInfo();
}

void FFMS_Track::GeneratePublicInfo() {
	RealFrameNumbers.reserve(size());
	PublicFrameInfo.reserve(size());
	for (size_t i = 0; i < size(); ++i) {
		if (Frames[i].Hidden)
            continue;
        RealFrameNumbers.push_back(static_cast<int>(i));

		FFMS_FrameInfo info = {Frames[i].PTS, Frames[i].RepeatPict, Frames[Frames[i].OriginalPos].KeyFrame, Frames[i].FilePos, i};
		PublicFrameInfo.push_back(info);
	}
}

const FFMS_FrameInfo *FFMS_Track::GetFrameInfo(size_t N) const {
	if (N >= PublicFrameInfo.size()) return nullptr;
	return &PublicFrameInfo[N];
}
