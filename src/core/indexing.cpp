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

#if LIBAVUTIL_VERSION_INT > AV_VERSION_INT(50, 40, 1)
#include <libavutil/sha.h>
#else
extern const int av_sha_size;
struct AVSHA;
int av_sha_init(struct AVSHA* context, int bits);
void av_sha_update(struct AVSHA* context, const uint8_t* data, unsigned int len);
void av_sha_final(struct AVSHA* context, uint8_t *digest);
#endif

#include <zlib.h>
}

#undef max

#define INDEXID 0x53920873
#ifdef __MINGW64__
	#define ARCH 1
#elif defined(__MINGW32__)
	#define ARCH 2
#elif defined(_WIN64)
	#define ARCH 6
#elif defined(_WIN32)
	#define ARCH 3
#elif defined(__i386__) //*nix 32bit
	#define ARCH 4
#else //*nix 64bit
	#define ARCH 5
#endif

extern bool HasHaaliMPEG;
extern bool HasHaaliOGG;

#ifndef FFMS_USE_POSTPROC
unsigned postproc_version() { return 0; } // ugly workaround to avoid lots of ifdeffing
#endif // FFMS_USE_POSTPROC

namespace {
struct IndexHeader {
	uint32_t Id;
	uint32_t Version;
	uint32_t Arch;
	uint32_t Tracks;
	uint32_t Decoder;
	uint32_t LAVUVersion;
	uint32_t LAVFVersion;
	uint32_t LAVCVersion;
	uint32_t LSWSVersion;
	uint32_t LPPVersion;
	int64_t FileSize;
	uint8_t FileSignature[20];
};

struct TrackHeader {
	uint32_t TT;
	uint32_t Frames;
	int64_t Num;
	int64_t Den;
	uint32_t UseDTS;
	uint32_t HasTS;
	uint32_t NumInvisible;
};

struct zipped_file {
	ffms_fstream *Index;
	z_stream *stream;
	unsigned char in_buffer[65536];

	template<class T>
	void read(T *out) {
		if (!out) return;

		stream->next_out = reinterpret_cast<unsigned char *>(out);
		stream->avail_out = sizeof(T);
		do {
			if (!stream->avail_in) {
				Index->read(reinterpret_cast<char *>(in_buffer), sizeof(in_buffer));
				stream->next_in = in_buffer;
				stream->avail_in = Index->gcount();
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

TFrameInfo::TFrameInfo(int64_t PTS, int64_t SampleStart, unsigned int SampleCount, int RepeatPict, bool KeyFrame, int64_t FilePos, unsigned int FrameSize, int FrameType)
: SampleStart(SampleStart)
, SampleCount(SampleCount)
, FilePos(FilePos)
, FrameSize(FrameSize)
, OriginalPos(0)
, FrameType(FrameType)
{
	this->PTS = PTS;
	this->RepeatPict = RepeatPict;
	this->KeyFrame = KeyFrame;
}

void FFMS_Track::AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, unsigned int FrameSize, bool Invisible) {
	if (Invisible)
		InvisibleFrames.push_back(Frames.size() - InvisibleFrames.size());

	Frames.push_back(TFrameInfo(PTS, 0, 0, RepeatPict, KeyFrame, FilePos, FrameSize, FrameType));
}

void FFMS_Track::AddAudioFrame(int64_t PTS, int64_t SampleStart, int64_t SampleCount, bool KeyFrame, int64_t FilePos, unsigned int FrameSize) {
	if (SampleCount > 0)
		Frames.push_back(TFrameInfo(PTS, SampleStart, static_cast<unsigned int>(SampleCount), 0, KeyFrame, FilePos, FrameSize, 0));
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) const {
	ffms_fstream Timecodes(TimecodeFile, std::ios::out | std::ios::trunc);

	if (!Timecodes.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + TimecodeFile + "' for writing");

	Timecodes << "# timecode format v2\n";

	for (size_t i = 0; i < size(); ++i)
		Timecodes << std::fixed << ((Frames[i].PTS * TB.Num) / (double)TB.Den) << "\n";
}

static bool PTSComparison(TFrameInfo FI1, TFrameInfo FI2) {
	return FI1.PTS < FI2.PTS;
}

int FFMS_Track::FrameFromPTS(int64_t PTS) const {
	TFrameInfo F;
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
	TFrameInfo F;
	F.PTS = PTS;

	iterator Pos = std::lower_bound(begin(), end(), F, PTSComparison);
	if (Pos == end())
		return size() - 1;
	int Frame = std::distance(begin(), Pos);
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
	return Frame + distance(
		InvisibleFrames.begin(),
		upper_bound(InvisibleFrames.begin(), InvisibleFrames.end(), Frame));
}

int FFMS_Track::VisibleFrameCount() const {
	return Frames.size() - InvisibleFrames.size();
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

	// Swap the presentation time stamps of each b-frame with that of the frame
	// before it
	for (size_t i = 1; i < size(); ++i) {
		if (Frames[i].FrameType == AV_PICTURE_TYPE_B)
			std::swap(Frames[i].PTS, Frames[i - 1].PTS);
	}
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
	ReorderTemp.resize(size());

	for (size_t i = 0; i < size(); i++)
		ReorderTemp[i] = Frames[i].OriginalPos;

	for (size_t i = 0; i < size(); i++)
		Frames[ReorderTemp[i]].OriginalPos = i;
}

void FFMS_Track::Write(zipped_file *stream) const {
	TrackHeader TH = { TT, size(), TB.Num, TB.Den, UseDTS, HasTS, InvisibleFrames.size() };
	stream->write(TH);

	if (empty()) return;

	stream->write(Frames[0]);
	for (size_t i = 1; i < size(); ++i) {
		TFrameInfo temp = Frames[i];
		temp.FilePos -= Frames[i - 1].FilePos;
		temp.OriginalPos -= Frames[i - 1].OriginalPos;
		temp.PTS -= Frames[i - 1].PTS;
		temp.SampleStart -= Frames[i - 1].SampleStart;
		stream->write(temp);
	}

	if (InvisibleFrames.empty()) return;

	stream->write(InvisibleFrames[0]);
	for (size_t i = 1; i < InvisibleFrames.size(); ++i) {
		int temp = InvisibleFrames[i] - InvisibleFrames[i - 1];
		stream->write(temp);
	}
}

FFMS_Track::FFMS_Track() {
	this->TT = FFMS_TYPE_UNKNOWN;
	this->TB.Num = 0;
	this->TB.Den = 0;
	this->UseDTS = false;
	this->HasTS = true;
}

FFMS_Track::FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool UseDTS, bool HasTS) {
	this->TT = TT;
	this->TB.Num = Num;
	this->TB.Den = Den;
	this->UseDTS = UseDTS;
	this->HasTS = HasTS;
}

FFMS_Track::FFMS_Track(zipped_file &Stream) {
	TrackHeader TH;
	Stream.read(&TH);

	TT = static_cast<FFMS_TrackType>(TH.TT);
	TB.Num = TH.Num;
	TB.Den = TH.Den;
	UseDTS = !!TH.UseDTS;
	HasTS  = !!TH.HasTS;

	Frames.resize(TH.Frames);
	for (size_t i = 0; i < TH.Frames; ++i) {
		Stream.read(&Frames[i]);

		if (i > 0) {
			Frames[i].FilePos = Frames[i].FilePos + Frames[i - 1].FilePos;
			Frames[i].OriginalPos = Frames[i].OriginalPos + Frames[i - 1].OriginalPos;
			Frames[i].PTS = Frames[i].PTS + Frames[i - 1].PTS;
			Frames[i].SampleStart = Frames[i].SampleStart + Frames[i - 1].SampleStart;
		}
	}

	InvisibleFrames.resize(TH.NumInvisible);
	for (size_t i = 0; i < TH.NumInvisible; ++i)
		Stream.read(&InvisibleFrames[i]);
	partial_sum(InvisibleFrames.begin(), InvisibleFrames.end(), InvisibleFrames.begin());
}

void FFMS_Index::CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]) {
	FILE *SFile = ffms_fopen(Filename,"rb");
	if (!SFile)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + Filename + "' for hashing");

	std::vector<uint8_t> FileBuffer(1024*1024);
	std::vector<uint8_t> ctxmem(av_sha_size);
	AVSHA *ctx = (AVSHA*)(&ctxmem[0]);
	av_sha_init(ctx, 160);

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
	IndexHeader IH;
	IH.Id = INDEXID;
	IH.Version = FFMS_VERSION;
	IH.Arch = ARCH;
	IH.Tracks = size();
	IH.Decoder = Decoder;
	IH.LAVUVersion = avutil_version();
	IH.LAVFVersion = avformat_version();
	IH.LAVCVersion = avcodec_version();
	IH.LSWSVersion = swscale_version();
	IH.LPPVersion = postproc_version();
	IH.FileSize = Filesize;
	memcpy(IH.FileSignature, Digest, sizeof(Digest));

	zipped_file zf = { &IndexStream, &stream };
	zf.write(IH);

	for (unsigned int i = 0; i < IH.Tracks; i++)
		at(i).Write(&zf);

	zf.finish();
}

void FFMS_Index::ReadIndex(const char *IndexFile) {
	ffms_fstream Index(IndexFile, std::ios::in | std::ios::binary);

	if (!Index.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + IndexFile + "' for reading");

	z_stream stream;
	memset(&stream, 0, sizeof(z_stream));
	if (inflateInit(&stream) != Z_OK)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Failed to initialize zlib");

	// Read the index file header
	zipped_file inf = { &Index, &stream };
	IndexHeader IH;
	inf.read(&IH);

	if (IH.Id != INDEXID)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not a valid index file");

	if (IH.Version != FFMS_VERSION)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not the expected index version");

	if (IH.Arch != ARCH)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' was not made with this FFMS2 binary");

	if (IH.LAVUVersion != avutil_version() || IH.LAVFVersion != avformat_version() ||
		IH.LAVCVersion != avcodec_version() || IH.LSWSVersion != swscale_version() ||
		IH.LPPVersion != postproc_version())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("A different FFmpeg build was used to create '") + IndexFile + "'");

	if (!(IH.Decoder & FFMS_GetEnabledSources()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"The source which this index was created with is not available");

	Decoder = IH.Decoder;
	Filesize = IH.FileSize;
	memcpy(Digest, IH.FileSignature, sizeof(Digest));

	try {
		for (unsigned int i = 0; i < IH.Tracks; i++) {
			push_back(FFMS_Track(inf));
		}
	}
	catch (FFMS_Exception const&) {
		throw;
	}
	catch (...) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Unknown error while reading index information in '") + IndexFile + "'");
	}
}

FFMS_Index::FFMS_Index() : RefCount(1) {
}

FFMS_Index::FFMS_Index(int64_t Filesize, uint8_t Digest[20]) : RefCount(1), Filesize(Filesize) {
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
, DecodingBuffer(AVCODEC_MAX_AUDIO_FRAME_SIZE * 10)
{
	FFMS_Index::CalculateFileSignature(Filename, &Filesize, Digest);
}

FFMS_Indexer::~FFMS_Indexer() {

}

void FFMS_Indexer::WriteAudio(SharedAudioContext &AudioContext, FFMS_Index *Index, int Track, int DBSize) {
	// Delay writer creation until after an audio frame has been decoded. This ensures that all parameters are known when writing the headers.
	if (DBSize <= 0) return;

	if (!AudioContext.W64Writer) {
		FFMS_AudioProperties AP;
		FillAP(AP, AudioContext.CodecContext, (*Index)[Track]);
		int FNSize = (*ANC)(SourceFile.c_str(), Track, &AP, NULL, 0, ANCPrivate);
		if (FNSize <= 0) {
			DumpMask = DumpMask & ~(1 << Track);
			return;
		}

		std::vector<char> WName(FNSize);
		(*ANC)(SourceFile.c_str(), Track, &AP, &WName[0], FNSize, ANCPrivate);
		std::string WN(&WName[0]);
		try {
			AudioContext.W64Writer =
				new Wave64Writer(WN.c_str(),
					av_get_bytes_per_sample(AudioContext.CodecContext->sample_fmt),
					AudioContext.CodecContext->channels,
					AudioContext.CodecContext->sample_rate,
					(AudioContext.CodecContext->sample_fmt == AV_SAMPLE_FMT_FLT) || (AudioContext.CodecContext->sample_fmt == AV_SAMPLE_FMT_DBL));
		} catch (...) {
			throw FFMS_Exception(FFMS_ERROR_WAVE_WRITER, FFMS_ERROR_FILE_WRITE,
				"Failed to write wave data");
		}
	}

	AudioContext.W64Writer->WriteData(&DecodingBuffer[0], DBSize);
}

int64_t FFMS_Indexer::IndexAudioPacket(int Track, AVPacket *Packet, SharedAudioContext &Context, FFMS_Index &TrackIndices) {
	AVCodecContext *CodecContext = Context.CodecContext;
	int64_t StartSample = Context.CurrentSample;
	int Read = 0;
	while (Packet->size > 0) {
		int dbsize = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
		int Ret = avcodec_decode_audio3(CodecContext, (int16_t *)&DecodingBuffer[0], &dbsize, Packet);
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

		CheckAudioProperties(Track, CodecContext);

		if (dbsize > 0)
			Context.CurrentSample += dbsize / (av_get_bytes_per_sample(CodecContext->sample_fmt) * CodecContext->channels);

		if (DumpMask & (1 << Track))
			WriteAudio(Context, &TrackIndices, Track, dbsize);
	}
	Packet->size += Read;
	Packet->data -= Read;
	return Context.CurrentSample - StartSample;
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
