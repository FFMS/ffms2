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
	FrameInfo f = {0};
	f.PTS = stream.Read<int64_t>() + prev.PTS;
	f.KeyFrame = !!stream.Read<int8_t>();
	f.FilePos = stream.Read<int64_t>() + prev.FilePos + prev.FrameSize;
	f.FrameSize = stream.Read<uint32_t>();
	f.OriginalPos = static_cast<size_t>(stream.Read<uint64_t>() + prev.OriginalPos + 1);

	if (TT == FFMS_TYPE_AUDIO) {
		f.SampleStart = prev.SampleStart + prev.SampleCount;
		f.SampleCount = stream.Read<uint32_t>() + prev.SampleCount;
	}
	else if (TT == FFMS_TYPE_VIDEO) {
		f.FrameType = stream.Read<uint8_t>();
		f.RepeatPict = stream.Read<int32_t>();
		f.Hidden = !!stream.Read<uint8_t>();
	}
	return f;
}

static void WriteFrame(ZipFile &stream, FrameInfo const& f, FrameInfo const& prev, const FFMS_TrackType TT) {
	stream.Write(f.PTS - prev.PTS);
	stream.Write<int8_t>(f.KeyFrame);
	stream.Write(f.FilePos - prev.FilePos - prev.FrameSize);
	stream.Write(f.FrameSize);
	stream.Write(static_cast<uint64_t>(f.OriginalPos) - prev.OriginalPos - 1);

	if (TT == FFMS_TYPE_AUDIO)
		stream.Write(f.SampleCount - prev.SampleCount);
	else if (TT == FFMS_TYPE_VIDEO) {
		stream.Write<uint8_t>(f.FrameType);
		stream.Write<int32_t>(f.RepeatPict);
		stream.Write<uint8_t>(f.Hidden);
	}
}
}

FFMS_Track::FFMS_Track()
: TT(FFMS_TYPE_UNKNOWN)
, UseDTS(false)
, HasTS(true)
{
	this->TB.Num = 0;
	this->TB.Den = 0;
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
	size_t FrameCount = static_cast<size_t>(stream.Read<uint64_t>());
	TB.Num = stream.Read<int64_t>();
	TB.Den = stream.Read<int64_t>();
	UseDTS = !!stream.Read<uint8_t>();
	HasTS = !!stream.Read<uint8_t>();

	if (!FrameCount) return;

	FrameInfo temp = {0};
	Frames.reserve(FrameCount);
	for (size_t i = 0; i < FrameCount; ++i)
		Frames.push_back(ReadFrame(stream, i == 0 ? temp : Frames.back(), TT));

	size_t RealCount = static_cast<size_t>(stream.Read<uint64_t>());
	RealFrameNumbers.reserve(RealCount);
	for (size_t i = 0; i < RealCount; ++i)
		RealFrameNumbers.push_back(stream.Read<uint32_t>() + 1 + (i == 0 ? 0 : RealFrameNumbers.back()));
}

void FFMS_Track::Write(ZipFile &stream) const {
	stream.Write<uint8_t>(TT);
	stream.Write<uint64_t>(size());
	stream.Write(TB.Num);
	stream.Write(TB.Den);
	stream.Write<uint8_t>(UseDTS);
	stream.Write<uint8_t>(HasTS);

	if (empty()) return;

	FrameInfo temp = {0};
	for (size_t i = 0; i < size(); ++i)
		WriteFrame(stream, Frames[i], i == 0 ? temp : Frames[i - 1], TT);

	stream.Write<uint64_t>(RealFrameNumbers.size());
	for (size_t i = 0; i < RealFrameNumbers.size(); ++i)
		stream.Write<uint32_t>(RealFrameNumbers[i] - 1 - (i == 0 ? 0 : RealFrameNumbers[i - 1]));
}

void FFMS_Track::AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, uint32_t FrameSize, bool Hidden) {
	if (!Hidden)
		RealFrameNumbers.push_back(Frames.size());

	FrameInfo f = {PTS, FilePos, 0, 0, FrameSize, 0, FrameType, RepeatPict, KeyFrame, Hidden};
	Frames.push_back(f);
}

void FFMS_Track::AddAudioFrame(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, bool KeyFrame, int64_t FilePos, uint32_t FrameSize) {
	if (SampleCount > 0) {
		FrameInfo f = {PTS, FilePos, SampleStart, SampleCount, FrameSize, 0, 0, 0, KeyFrame, false};
		Frames.push_back(f);
	}
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) const {
	FileHandle file(TimecodeFile, "w", FFMS_ERROR_TRACK, FFMS_ERROR_FILE_WRITE);

	file.Printf("# timecode format v2\n");
	for (size_t i = 0; i < size(); ++i) {
		if (!Frames[i].Hidden)
			file.Printf("%g\n", (Frames[i].PTS * TB.Num) / (double)TB.Den);
	}
}

static bool PTSComparison(FrameInfo FI1, FrameInfo FI2) {
	return FI1.PTS < FI2.PTS;
}

int FFMS_Track::FrameFromPTS(int64_t PTS) const {
	FrameInfo F;
	F.PTS = PTS;

	iterator Pos = std::lower_bound(begin(), end(), F, PTSComparison);
	if (Pos == end() || Pos->PTS != PTS)
		return -1;
	return std::distance(begin(), Pos);
}

int FFMS_Track::FrameFromPos(int64_t Pos) const {
	for (size_t i = 0; i < size(); i++)
	if (Frames[i].FilePos == Pos)
		return i;
	return -1;
}

int FFMS_Track::ClosestFrameFromPTS(int64_t PTS) const {
	FrameInfo F;
	F.PTS = PTS;

	iterator Pos = std::lower_bound(begin(), end(), F, PTSComparison);
	if (Pos == end())
		return size() - 1;
	size_t Frame = std::distance(begin(), Pos);
	if (Pos == begin() || FFABS(Pos->PTS - PTS) <= FFABS((Pos - 1)->PTS - PTS))
		return Frame;
	return Frame - 1;
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
	return RealFrameNumbers.size();
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
	bool prev_valid_pos = Frames[0].FilePos >= 0;
	for (size_t i = 1; i < size(); ++i) {
		bool valid_pos = Frames[i].FilePos >= 0;
		if (valid_pos == prev_valid_pos)
			return;
		prev_valid_pos = valid_pos;
	}

	int Offset = 0;
	for (size_t i = 0; i < size(); ++i) {
		if (Frames[i].FilePos < 0 && !Frames[i].Hidden) {
			Frames[i].Hidden = true;
			++Offset;
		}
		else if (Offset)
			RealFrameNumbers[i - Offset] = RealFrameNumbers[i];
	}

	if (Offset)
		RealFrameNumbers.resize(RealFrameNumbers.size() - Offset);
}

void FFMS_Track::SortByPTS() {
	// With some formats (such as Vorbis) a bad final packet results in a
	// frame with PTS 0, which we don't want to sort to the beginning
	if (size() > 2 && front().PTS >= back().PTS)
		Frames.pop_back();

	for (size_t i = 0; i < size(); i++)
		Frames[i].OriginalPos = i;

	if (TT != FFMS_TYPE_VIDEO)
		return;

	MaybeReorderFrames();

	sort(Frames.begin(), Frames.end(), PTSComparison);

	std::vector<size_t> ReorderTemp;
	ReorderTemp.reserve(size());

	for (size_t i = 0; i < size(); i++)
		ReorderTemp.push_back(Frames[i].OriginalPos);

	for (size_t i = 0; i < size(); i++)
		Frames[ReorderTemp[i]].OriginalPos = i;

	MaybeHideFrames();
}

const FFMS_FrameInfo *FFMS_Track::GetFrameInfo(size_t N) const {
	if (TT != FFMS_TYPE_VIDEO) return NULL;

	if (PublicFrameInfo.empty()) {
		PublicFrameInfo.reserve(VisibleFrameCount());
		for (size_t i = 0; i < size(); ++i) {
			if (Frames[i].Hidden) continue;
			FFMS_FrameInfo info = {Frames[i].PTS, Frames[i].RepeatPict, Frames[i].KeyFrame};
			PublicFrameInfo.push_back(info);
		}
	}

	return N < PublicFrameInfo.size() ? &PublicFrameInfo[N] : NULL;
}
