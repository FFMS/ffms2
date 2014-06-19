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
#include "track.h"
#include "wave64writer.h"
#include "zipfile.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/sha.h>
}

#define INDEXID 0x53920873

extern bool HasHaaliMPEG;
extern bool HasHaaliOGG;

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

void ffms_free_sha(AVSHA **ctx) { av_freep(ctx); }

void FFMS_Index::CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]) {
	FileHandle file(Filename, "rb", FFMS_ERROR_INDEX, FFMS_ERROR_FILE_READ);

#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 51, 43, 0, 51, 75, 100)
	unknown_size<AVSHA, av_sha_alloc, ffms_free_sha> ctx;
#else
	std::vector<uint8_t> ctxmem(av_sha_size);
	AVSHA *ctx = (AVSHA*)(&ctxmem[0]);
#endif
	av_sha_init(ctx, 160);

	try {
		file.Seek(0, SEEK_END);
		*Filesize = file.Tell();

		file.Seek(0, SEEK_SET);
		std::vector<char> FileBuffer(static_cast<size_t>(std::min<int64_t>(1024*1024, *Filesize)));
		size_t BytesRead = file.Read(&FileBuffer[0], FileBuffer.size());
		av_sha_update(ctx, reinterpret_cast<const uint8_t*>(&FileBuffer[0]), BytesRead);

		if (*Filesize > static_cast<int64_t>(FileBuffer.size())) {
			file.Seek(-(int)FileBuffer.size(), SEEK_END);
			BytesRead = file.Read(&FileBuffer[0], FileBuffer.size());
			av_sha_update(ctx, reinterpret_cast<const uint8_t*>(&FileBuffer[0]), BytesRead);
		}
	}
	catch (...) {
		av_sha_final(ctx, Digest);
		throw;
	}
	av_sha_final(ctx, Digest);
}

void FFMS_Index::AddRef() {
	++RefCount;
}

void FFMS_Index::Release() {
	if (--RefCount == 0)
		delete this;
}

void FFMS_Index::Finalize(std::vector<SharedVideoContext> const& video_contexts) {
	for (size_t i = 0, end = size(); i != end; ++i) {
		FFMS_Track& track = (*this)[i];
		track.FinalizeTrack();

		if (track.TT != FFMS_TYPE_VIDEO) continue;

		if (video_contexts[i].CodecContext->has_b_frames) {
			track.MaxBFrames = video_contexts[i].CodecContext->has_b_frames;
			continue;
		}

		// Whether or not has_b_frames gets set during indexing seems
		// to vary based on version of FFmpeg/Libav, so do an extra
		// check for b-frames if it's 0.
		for (size_t f = 0; f < track.size(); ++f) {
			if (track[f].FrameType == AV_PICTURE_TYPE_B) {
				track.MaxBFrames = 1;
				break;
			}
		}
	}
}

bool FFMS_Index::CompareFileSignature(const char *Filename) {
	int64_t CFilesize;
	uint8_t CDigest[20];
	CalculateFileSignature(Filename, &CFilesize, CDigest);
	return (CFilesize == Filesize && !memcmp(CDigest, Digest, sizeof(Digest)));
}

void FFMS_Index::WriteIndex(const char *IndexFile) {
	ZipFile zf(IndexFile, "wb");

	// Write the index file header
	zf.Write<uint32_t>(INDEXID);
	zf.Write<uint32_t>(FFMS_VERSION);
	zf.Write<uint32_t>(size());
	zf.Write<uint32_t>(Decoder);
	zf.Write<uint32_t>(ErrorHandling);
	zf.Write<uint32_t>(avutil_version());
	zf.Write<uint32_t>(avformat_version());
	zf.Write<uint32_t>(avcodec_version());
	zf.Write<uint32_t>(swscale_version());
	zf.Write<int64_t>(Filesize);
	zf.Write(Digest);

	for (size_t i = 0; i < size(); ++i)
		at(i).Write(zf);

	zf.Finish();
}

FFMS_Index::FFMS_Index(const char *IndexFile)
: RefCount(1)
{
	ZipFile zf(IndexFile, "rb");

	// Read the index file header
	if (zf.Read<uint32_t>() != INDEXID)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not a valid index file");

	if (zf.Read<uint32_t>() != FFMS_VERSION)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not the expected index version");

	uint32_t Tracks = zf.Read<uint32_t>();
	Decoder = zf.Read<uint32_t>();
	ErrorHandling = zf.Read<uint32_t>();

	if (!(Decoder & FFMS_GetEnabledSources()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"The source which this index was created with is not available");

	if (zf.Read<uint32_t>() != avutil_version() ||
		zf.Read<uint32_t>() != avformat_version() ||
		zf.Read<uint32_t>() != avcodec_version() ||
		zf.Read<uint32_t>() != swscale_version())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("A different FFmpeg build was used to create '") + IndexFile + "'");

	Filesize = zf.Read<int64_t>();
	zf.Read(Digest, sizeof(Digest));

	reserve(Tracks);
	try {
		for (size_t i = 0; i < Tracks; ++i)
			push_back(FFMS_Track(zf));
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

FFMS_Indexer *CreateIndexer(const char *Filename, FFMS_Sources Demuxer) {
	AVFormatContext *FormatContext = NULL;

	if (avformat_open_input(&FormatContext, Filename, NULL, NULL) != 0)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't open '") + Filename + "'");

	// Demuxer was not forced, probe for the best one to use
	if (Demuxer == FFMS_SOURCE_DEFAULT) {
		// Do matroska indexing instead?
		if (!strncmp(FormatContext->iformat->name, "matroska", 8)) {
			avformat_close_input(&FormatContext);
			return CreateMatroskaIndexer(Filename);
		}

#ifdef HAALISOURCE
		// Do haali ts indexing instead?
		if (HasHaaliMPEG && (!strcmp(FormatContext->iformat->name, "mpeg") || !strcmp(FormatContext->iformat->name, "mpegts"))) {
			avformat_close_input(&FormatContext);
			return CreateHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
		}

		if (HasHaaliOGG && !strcmp(FormatContext->iformat->name, "ogg")) {
			avformat_close_input(&FormatContext);
			return CreateHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		}
#endif

		return CreateLavfIndexer(Filename, FormatContext);
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
			return CreateLavfIndexer(Filename, FormatContext);
#ifdef HAALISOURCE
		case FFMS_SOURCE_HAALIOGG:
			if (!HasHaaliOGG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's Ogg parser is not available");
			return CreateHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		case FFMS_SOURCE_HAALIMPEG:
			if (!HasHaaliMPEG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's MPEG PS/TS parser is not available");
			return CreateHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
#endif
		case FFMS_SOURCE_MATROSKA:
			return CreateMatroskaIndexer(Filename);
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

		std::vector<char> WName(FNSize + 1);
		(*ANC)(SourceFile.c_str(), Track, &AP, &WName[0], FNSize, ANCPrivate);
		WName.back() = 0;
		try {
			AudioContext.W64Writer =
				new Wave64Writer(WName.data(),
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
