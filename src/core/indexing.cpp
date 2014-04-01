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

#include "indexing.h"

#include "codectype.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/sha.h>

#include <zlib.h>
}

#undef max

#define INDEXID 0x53920873

extern bool HasHaaliMPEG;
extern bool HasHaaliOGG;

namespace {
struct zipped_file {
	ffms_fstream *Index;
	z_stream *stream;
	unsigned char in_buffer[65536];

	template<class T>
	void read(T &out) {
		stream->next_out = reinterpret_cast<unsigned char *>(&out);
		stream->avail_out = sizeof(T);
		do {
			if (!stream->avail_in) {
				Index->read(reinterpret_cast<char *>(in_buffer), sizeof(in_buffer));
				stream->next_in = in_buffer;
				stream->avail_in = static_cast<uInt>(Index->gcount());
			}

			switch (inflate(stream, Z_SYNC_FLUSH)) {
				case Z_NEED_DICT:
					inflateEnd(stream);
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Dictionary error.");
				case Z_DATA_ERROR:
					inflateEnd(stream);
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Data error.");
				case Z_MEM_ERROR:
					inflateEnd(stream);
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Memory error.");
				case Z_STREAM_END:
					inflateEnd(stream);
					if (stream->avail_out > 0)
						throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Stream ended early");
			}
		} while (stream->avail_out);
	}

	template<typename T>
	T read() {
		T ret;
		read(ret);
		return ret;
	}

	template<class T>
	void write(T const& in) {
		stream->next_in = reinterpret_cast<unsigned char *>(const_cast<T*>(&in));
		stream->avail_in = sizeof(T);
		do {
			Bytef out[65536];
			stream->avail_out = sizeof(out);
			stream->next_out = out;
			deflate(stream, Z_NO_FLUSH);
			uInt have = sizeof(out) - stream->avail_out;
			if (have) Index->write(reinterpret_cast<char *>(out), have);
		} while (stream->avail_out == 0);
	}

	void finish() {
		stream->next_in = 0;
		stream->avail_in = 0;

		int ret;
		do {
			do {
				Bytef out[65536];
				stream->avail_out = sizeof(out);
				stream->next_out = out;
				ret = deflate(stream, Z_FINISH);
				uInt have = sizeof(out) - stream->avail_out;
				if (have) Index->write(reinterpret_cast<char *>(out), have);
			} while (stream->avail_out == 0);
		} while (ret != Z_STREAM_END);
		deflateEnd(stream);
	}
};

}

SharedVideoContext::SharedVideoContext(bool FreeCodecContext)
: FreeCodecContext(FreeCodecContext)
, CodecContext(NULL)
, Parser(NULL)
, BitStreamFilter(NULL)
, TCC(NULL)
{
}

SharedVideoContext::~SharedVideoContext() {
	if (CodecContext) {
		avcodec_close(CodecContext);
		if (FreeCodecContext)
			av_freep(&CodecContext);
	}
	av_parser_close(Parser);
	if (BitStreamFilter)
		av_bitstream_filter_close(BitStreamFilter);
	delete TCC;
}

SharedAudioContext::SharedAudioContext(bool FreeCodecContext)
: FreeCodecContext(FreeCodecContext)
, CodecContext(NULL)
, W64Writer(NULL)
, CurrentSample(0)
, TCC(NULL)
{
}

SharedAudioContext::~SharedAudioContext() {
	delete W64Writer;
	if (CodecContext) {
		avcodec_close(CodecContext);
		if (FreeCodecContext)
			av_freep(&CodecContext);
	}
	delete TCC;
}

static FrameInfo ReadFrame(zipped_file &stream, FrameInfo const& prev, const FFMS_TrackType TT) {
	FrameInfo f = {0};
	f.PTS = stream.read<int64_t>() + prev.PTS;
	f.KeyFrame = !!stream.read<int8_t>();
	f.FilePos = stream.read<int64_t>() + prev.FilePos + prev.FrameSize;
	f.FrameSize = stream.read<uint32_t>();
	f.OriginalPos = static_cast<size_t>(stream.read<uint64_t>() + prev.OriginalPos + 1);

	if (TT == FFMS_TYPE_AUDIO) {
		f.SampleStart = prev.SampleStart + prev.SampleCount;
		f.SampleCount = stream.read<uint32_t>() + prev.SampleCount;
	}
	else if (TT == FFMS_TYPE_VIDEO) {
		f.FrameType = stream.read<uint8_t>();
		f.RepeatPict = stream.read<int32_t>();
		f.Hidden = !!stream.read<uint8_t>();
	}
	return f;
}

static void WriteFrame(zipped_file &stream, FrameInfo const& f, FrameInfo const& prev, const FFMS_TrackType TT) {
	stream.write(f.PTS - prev.PTS);
	stream.write<int8_t>(f.KeyFrame);
	stream.write(f.FilePos - prev.FilePos - prev.FrameSize);
	stream.write(f.FrameSize);
	stream.write(static_cast<uint64_t>(f.OriginalPos) - prev.OriginalPos - 1);

	if (TT == FFMS_TYPE_AUDIO)
		stream.write(f.SampleCount - prev.SampleCount);
	else if (TT == FFMS_TYPE_VIDEO) {
		stream.write<uint8_t>(f.FrameType);
		stream.write<int32_t>(f.RepeatPict);
		stream.write<uint8_t>(f.Hidden);
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

FFMS_Track::FFMS_Track(zipped_file &stream) {
	TT = static_cast<FFMS_TrackType>(stream.read<uint8_t>());
	size_t FrameCount = static_cast<size_t>(stream.read<uint64_t>());
	stream.read(TB.Num);
	stream.read(TB.Den);
	UseDTS = !!stream.read<uint8_t>();
	HasTS = !!stream.read<uint8_t>();

	if (!FrameCount) return;

	::FrameInfo temp = {0};
	Frames.reserve(FrameCount);
	for (size_t i = 0; i < FrameCount; ++i)
		Frames.push_back(ReadFrame(stream, i == 0 ? temp : Frames.back(), TT));

	size_t RealCount = static_cast<size_t>(stream.read<uint64_t>());
	RealFrameNumbers.reserve(RealCount);
	for (size_t i = 0; i < RealCount; ++i)
		RealFrameNumbers.push_back(stream.read<uint32_t>() + 1 + (i == 0 ? 0 : RealFrameNumbers.back()));
}

void FFMS_Track::Write(zipped_file &stream) const {
	stream.write<uint8_t>(TT);
	stream.write<uint64_t>(size());
	stream.write(TB.Num);
	stream.write(TB.Den);
	stream.write<uint8_t>(UseDTS);
	stream.write<uint8_t>(HasTS);

	if (empty()) return;

	::FrameInfo temp = {0};
	for (size_t i = 0; i < size(); ++i)
		WriteFrame(stream, Frames[i], i == 0 ? temp : Frames[i - 1], TT);

	stream.write<uint64_t>(RealFrameNumbers.size());
	for (size_t i = 0; i < RealFrameNumbers.size(); ++i)
		stream.write<uint32_t>(RealFrameNumbers[i] - 1 - (i == 0 ? 0 : RealFrameNumbers[i - 1]));
}

void FFMS_Track::AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, uint32_t FrameSize, bool Hidden) {
	if (!Hidden)
		RealFrameNumbers.push_back(Frames.size());

	::FrameInfo f = {PTS, FilePos, 0, 0, FrameSize, 0, FrameType, RepeatPict, KeyFrame, Hidden};
	Frames.push_back(f);
}

void FFMS_Track::AddAudioFrame(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, bool KeyFrame, int64_t FilePos, uint32_t FrameSize) {
	if (SampleCount > 0) {
		::FrameInfo f = {PTS, FilePos, SampleStart, SampleCount, FrameSize, 0, 0, 0, KeyFrame, false};
		Frames.push_back(f);
	}
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) const {
	ffms_fstream Timecodes(TimecodeFile, std::ios::out | std::ios::trunc);

	if (!Timecodes.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + TimecodeFile + "' for writing");

	Timecodes << "# timecode format v2\n";

	for (size_t i = 0; i < size(); ++i) {
		if (!Frames[i].Hidden)
			Timecodes << std::fixed << ((Frames[i].PTS * TB.Num) / (double)TB.Den) << "\n";
	}
}

static bool PTSComparison(FrameInfo FI1, FrameInfo FI2) {
	return FI1.PTS < FI2.PTS;
}

int FFMS_Track::FrameFromPTS(int64_t PTS) const {
	::FrameInfo F;
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
	::FrameInfo F;
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
	Frame = FFMIN(FFMAX(Frame, 0), static_cast<int>(size()) - 1);
	for (; Frame > 0 && !Frames[Frame].KeyFrame; Frame--) ;
	for (; Frame > 0 && !Frames[Frames[Frame].OriginalPos].KeyFrame; Frame--) ;
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

const FFMS_FrameInfo *FFMS_Track::FrameInfo(size_t N) const {
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

void ffms_free_sha(AVSHA **ctx) { av_freep(ctx); }

void FFMS_Index::CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]) {
	FILE *SFile = ffms_fopen(Filename, "rb");
	if (!SFile)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + Filename + "' for hashing");

#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 51, 43, 0, 51, 75, 100)
	unknown_size<AVSHA, av_sha_alloc, ffms_free_sha> ctx;
#else
	std::vector<uint8_t> ctxmem(av_sha_size);
	AVSHA *ctx = (AVSHA*)(&ctxmem[0]);
#endif
	av_sha_init(ctx, 160);

	std::vector<uint8_t> FileBuffer(1024*1024);
	try {
		size_t BytesRead = fread(&FileBuffer[0], 1, FileBuffer.size(), SFile);
		if (ferror(SFile) && !feof(SFile))
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Failed to read '") + Filename + "' for hashing");

		av_sha_update(ctx, &FileBuffer[0], BytesRead);

		fseeko(SFile, -(int)FileBuffer.size(), SEEK_END);
		BytesRead = fread(&FileBuffer[0], 1, FileBuffer.size(), SFile);
		if (ferror(SFile) && !feof(SFile)) {
			std::ostringstream buf;
			buf << "Failed to seek with offset " << FileBuffer.size() << " from file end in '" << Filename << "' for hashing";
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
		}

		av_sha_update(ctx, &FileBuffer[0], BytesRead);

		fseeko(SFile, 0, SEEK_END);
		if (ferror(SFile))
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Failed to seek to end of '") + Filename + "' for hashing");

		*Filesize = ftello(SFile);
	}
	catch (...) {
		fclose(SFile);
		av_sha_final(ctx, Digest);
		throw;
	}
	fclose(SFile);
	av_sha_final(ctx, Digest);
}

int FFMS_Index::AddRef() {
	return ++RefCount;
}

int FFMS_Index::Release() {
	int Temp = --RefCount;
	if (!RefCount)
		delete this;
	return Temp;
}

void FFMS_Index::Sort() {
	for (FFMS_Index::iterator Cur = begin(); Cur != end(); ++Cur)
		Cur->SortByPTS();
}

bool FFMS_Index::CompareFileSignature(const char *Filename) {
	int64_t CFilesize;
	uint8_t CDigest[20];
	CalculateFileSignature(Filename, &CFilesize, CDigest);
	return (CFilesize == Filesize && !memcmp(CDigest, Digest, sizeof(Digest)));
}

void FFMS_Index::WriteIndex(const char *IndexFile) {
	ffms_fstream IndexStream(IndexFile, std::ios::out | std::ios::binary | std::ios::trunc);

	if (!IndexStream.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + IndexFile + "' for writing");

	z_stream stream;
	memset(&stream, 0, sizeof(z_stream));
	if (deflateInit(&stream, 9) != Z_OK) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to initialize zlib");
	}

	// Write the index file header
	zipped_file zf = { &IndexStream, &stream };
	zf.write<uint32_t>(INDEXID);
	zf.write<uint32_t>(FFMS_VERSION);
	zf.write<uint32_t>(size());
	zf.write<uint32_t>(Decoder);
	zf.write<uint32_t>(ErrorHandling);
	zf.write<uint32_t>(avutil_version());
	zf.write<uint32_t>(avformat_version());
	zf.write<uint32_t>(avcodec_version());
	zf.write<uint32_t>(swscale_version());
	zf.write<int64_t>(Filesize);
	zf.write(Digest);

	for (size_t i = 0; i < size(); ++i)
		at(i).Write(zf);

	zf.finish();
}

FFMS_Index::FFMS_Index(const char *IndexFile)
: RefCount(1)
{
	ffms_fstream Index(IndexFile, std::ios::in | std::ios::binary);

	if (!Index.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + IndexFile + "' for reading");

	Index.seekg(0, std::ios::end);
	if (!Index.tellg())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not a valid index file");

	Index.seekg(0, std::ios::beg);
	z_stream stream;
	memset(&stream, 0, sizeof(z_stream));
	if (inflateInit(&stream) != Z_OK)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Failed to initialize zlib");

	// Read the index file header
	zipped_file inf = { &Index, &stream };

	if (inf.read<uint32_t>() != INDEXID)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not a valid index file");

	if (inf.read<uint32_t>() != FFMS_VERSION)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not the expected index version");

	uint32_t Tracks = inf.read<uint32_t>();
	Decoder = inf.read<uint32_t>();
	ErrorHandling = inf.read<uint32_t>();

	if (!(Decoder & FFMS_GetEnabledSources()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"The source which this index was created with is not available");

	if (inf.read<uint32_t>() != avutil_version() ||
		inf.read<uint32_t>() != avformat_version() ||
		inf.read<uint32_t>() != avcodec_version() ||
		inf.read<uint32_t>() != swscale_version())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("A different FFmpeg build was used to create '") + IndexFile + "'");

	Filesize = inf.read<int64_t>();
	inf.read(Digest);

	reserve(Tracks);
	try {
		for (size_t i = 0; i < Tracks; ++i)
			push_back(FFMS_Track(inf));
	}
	catch (FFMS_Exception const&) {
		throw;
	}
	catch (...) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Unknown error while reading index information in '") + IndexFile + "'");
	}
}

FFMS_Index::FFMS_Index(int64_t Filesize, uint8_t Digest[20], int Decoder, int ErrorHandling)
: RefCount(1)
, Decoder(Decoder)
, ErrorHandling(ErrorHandling)
, Filesize(Filesize)
{
	memcpy(this->Digest, Digest, sizeof(this->Digest));
}

void FFMS_Indexer::SetIndexMask(int IndexMask) {
	this->IndexMask = IndexMask;
}

void FFMS_Indexer::SetDumpMask(int DumpMask) {
	this->DumpMask = DumpMask;
}

void FFMS_Indexer::SetErrorHandling(int ErrorHandling) {
	if (ErrorHandling != FFMS_IEH_ABORT && ErrorHandling != FFMS_IEH_CLEAR_TRACK &&
		ErrorHandling != FFMS_IEH_STOP_TRACK && ErrorHandling != FFMS_IEH_IGNORE)
		throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_INVALID_ARGUMENT,
			"Invalid error handling mode specified");
	this->ErrorHandling = ErrorHandling;
}

void FFMS_Indexer::SetProgressCallback(TIndexCallback IC, void *ICPrivate) {
	this->IC = IC;
	this->ICPrivate = ICPrivate;
}

void FFMS_Indexer::SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate) {
	this->ANC = ANC;
	this->ANCPrivate = ANCPrivate;
}

FFMS_Indexer *FFMS_Indexer::CreateIndexer(const char *Filename, FFMS_Sources Demuxer) {
	AVFormatContext *FormatContext = NULL;

	if (avformat_open_input(&FormatContext, Filename, NULL, NULL) != 0)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't open '") + Filename + "'");

	// Demuxer was not forced, probe for the best one to use
	if (Demuxer == FFMS_SOURCE_DEFAULT) {
		// Do matroska indexing instead?
		if (!strncmp(FormatContext->iformat->name, "matroska", 8)) {
			avformat_close_input(&FormatContext);
			return new FFMatroskaIndexer(Filename);
		}

#ifdef HAALISOURCE
		// Do haali ts indexing instead?
		if (HasHaaliMPEG && (!strcmp(FormatContext->iformat->name, "mpeg") || !strcmp(FormatContext->iformat->name, "mpegts"))) {
			avformat_close_input(&FormatContext);
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
		}

		if (HasHaaliOGG && !strcmp(FormatContext->iformat->name, "ogg")) {
			avformat_close_input(&FormatContext);
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		}
#endif

		return new FFLAVFIndexer(Filename, FormatContext);
	}

	// someone forced a demuxer, use it
	if (Demuxer != FFMS_SOURCE_LAVF)
		avformat_close_input(&FormatContext);
#if !defined(HAALISOURCE)
	if (Demuxer == FFMS_SOURCE_HAALIOGG || Demuxer == FFMS_SOURCE_HAALIMPEG) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Your binary was not compiled with support for Haali's DirectShow parsers");
	}
#endif // !defined(HAALISOURCE)

	switch (Demuxer) {
		case FFMS_SOURCE_LAVF:
			return new FFLAVFIndexer(Filename, FormatContext);
#ifdef HAALISOURCE
		case FFMS_SOURCE_HAALIOGG:
			if (!HasHaaliOGG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's Ogg parser is not available");
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		case FFMS_SOURCE_HAALIMPEG:
			if (!HasHaaliMPEG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's MPEG PS/TS parser is not available");
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
#endif
		case FFMS_SOURCE_MATROSKA:
			return new FFMatroskaIndexer(Filename);
		default:
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_INVALID_ARGUMENT, "Invalid demuxer requested");
	}
}

FFMS_Indexer::FFMS_Indexer(const char *Filename)
: IndexMask(0)
, DumpMask(0)
, ErrorHandling(FFMS_IEH_CLEAR_TRACK)
, IC(0)
, ICPrivate(0)
, ANC(0)
, ANCPrivate(0)
, SourceFile(Filename)
{
	FFMS_Index::CalculateFileSignature(Filename, &Filesize, Digest);
}

FFMS_Indexer::~FFMS_Indexer() {

}

void FFMS_Indexer::WriteAudio(SharedAudioContext &AudioContext, FFMS_Index *Index, int Track) {
	// Delay writer creation until after an audio frame has been decoded. This ensures that all parameters are known when writing the headers.
	if (DecodeFrame->nb_samples) return;

	if (!AudioContext.W64Writer) {
		FFMS_AudioProperties AP;
		FillAP(AP, AudioContext.CodecContext, (*Index)[Track]);
		int FNSize = (*ANC)(SourceFile.c_str(), Track, &AP, NULL, 0, ANCPrivate);
		if (FNSize <= 0) {
			DumpMask = DumpMask & ~(1 << Track);
			return;
		}

		int Format = av_get_packed_sample_fmt(AudioContext.CodecContext->sample_fmt);

		std::vector<char> WName(FNSize);
		(*ANC)(SourceFile.c_str(), Track, &AP, &WName[0], FNSize, ANCPrivate);
		std::string WN(&WName[0]);
		try {
			AudioContext.W64Writer =
				new Wave64Writer(WN.c_str(),
					av_get_bytes_per_sample(AudioContext.CodecContext->sample_fmt),
					AudioContext.CodecContext->channels,
					AudioContext.CodecContext->sample_rate,
					(Format == AV_SAMPLE_FMT_FLT) || (Format == AV_SAMPLE_FMT_DBL));
		} catch (...) {
			throw FFMS_Exception(FFMS_ERROR_WAVE_WRITER, FFMS_ERROR_FILE_WRITE,
				"Failed to write wave data");
		}
	}

	AudioContext.W64Writer->WriteData(*DecodeFrame);
}

uint32_t FFMS_Indexer::IndexAudioPacket(int Track, AVPacket *Packet, SharedAudioContext &Context, FFMS_Index &TrackIndices) {
	AVCodecContext *CodecContext = Context.CodecContext;
	int64_t StartSample = Context.CurrentSample;
	int Read = 0;
	while (Packet->size > 0) {
		DecodeFrame.reset();

		int GotFrame = 0;
		int Ret = avcodec_decode_audio4(CodecContext, DecodeFrame, &GotFrame, Packet);
		if (Ret < 0) {
			if (ErrorHandling == FFMS_IEH_ABORT) {
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING, "Audio decoding error");
			} else if (ErrorHandling == FFMS_IEH_CLEAR_TRACK) {
				TrackIndices[Track].clear();
				IndexMask &= ~(1 << Track);
			} else if (ErrorHandling == FFMS_IEH_STOP_TRACK) {
				IndexMask &= ~(1 << Track);
			}
			break;
		}
		Packet->size -= Ret;
		Packet->data += Ret;
		Read += Ret;

		if (GotFrame) {
			CheckAudioProperties(Track, CodecContext);

			Context.CurrentSample += DecodeFrame->nb_samples;

			if (DumpMask & (1 << Track))
				WriteAudio(Context, &TrackIndices, Track);
		}
	}
	Packet->size += Read;
	Packet->data -= Read;
	return static_cast<uint32_t>(Context.CurrentSample - StartSample);
}

void FFMS_Indexer::CheckAudioProperties(int Track, AVCodecContext *Context) {
	std::map<int, FFMS_AudioProperties>::iterator it = LastAudioProperties.find(Track);
	if (it == LastAudioProperties.end()) {
		FFMS_AudioProperties &AP = LastAudioProperties[Track];
		AP.SampleRate = Context->sample_rate;
		AP.SampleFormat = Context->sample_fmt;
		AP.Channels = Context->channels;
	}
	else if (it->second.SampleRate   != Context->sample_rate ||
			 it->second.SampleFormat != Context->sample_fmt ||
			 it->second.Channels     != Context->channels) {
		std::ostringstream buf;
		buf <<
			"Audio format change detected. This is currently unsupported."
			<< " Channels: " << it->second.Channels << " -> " << Context->channels << ";"
			<< " Sample rate: " << it->second.SampleRate << " -> " << Context->sample_rate << ";"
			<< " Sample format: " << GetLAVCSampleFormatName((AVSampleFormat)it->second.SampleFormat) << " -> "
			<< GetLAVCSampleFormatName(Context->sample_fmt);
		throw FFMS_Exception(FFMS_ERROR_UNSUPPORTED, FFMS_ERROR_DECODING, buf.str());
	}
}

void FFMS_Indexer::ParseVideoPacket(SharedVideoContext &VideoContext, AVPacket &pkt, int *RepeatPict, int *FrameType, bool *Invisible) {
	if (VideoContext.Parser) {
		uint8_t *OB;
		int OBSize;
		av_parser_parse2(VideoContext.Parser,
			VideoContext.CodecContext,
			&OB, &OBSize,
			pkt.data, pkt.size,
			pkt.pts, pkt.dts, pkt.pos);

		*RepeatPict = VideoContext.Parser->repeat_pict;
		*FrameType = VideoContext.Parser->pict_type;
		*Invisible = VideoContext.Parser->repeat_pict < 0;
	}
}
