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

#ifndef INDEXING_H
#define INDEXING_H

#include "utils.h"
#include "matroskareader.h"
#include "wave64writer.h"

#include <map>
#include <memory>

#ifdef HAALISOURCE
#	define WIN32_LEAN_AND_MEAN
#	define _WIN32_DCOM
#	include <windows.h>
#	include <tchar.h>
#	include <atlbase.h>
#	include <dshow.h>
#	include <initguid.h>
#	include "CoParser.h"
#	include "guids.h"
#endif

namespace { struct zipped_file; }

class SharedVideoContext {
	bool FreeCodecContext;
public:
	AVCodecContext *CodecContext;
	AVCodecParserContext *Parser;
	AVBitStreamFilterContext *BitStreamFilter;
	TrackCompressionContext *TCC;

	SharedVideoContext(bool FreeCodecContext);
	~SharedVideoContext();
};

class SharedAudioContext {
	bool FreeCodecContext;
public:
	AVCodecContext *CodecContext;
	Wave64Writer *W64Writer;
	int64_t CurrentSample;
	TrackCompressionContext *TCC;

	SharedAudioContext(bool FreeCodecContext);
	~SharedAudioContext();
};

struct TFrameInfo : public FFMS_FrameInfo {
public:
	int64_t SampleStart;
	uint32_t SampleCount;
	int64_t FilePos;
	uint32_t FrameSize;
	size_t OriginalPos;
	int FrameType;

	TFrameInfo() { }
	TFrameInfo(int64_t PTS, int64_t SampleStart, uint32_t SampleCount, int RepeatPict, bool KeyFrame, int64_t FilePos, uint32_t FrameSize, int FrameType);

	void Write(zipped_file &zf, TFrameInfo const& prev, FFMS_TrackType TT) const;
	void Read(zipped_file &zf, TFrameInfo const& prev, FFMS_TrackType TT);
};

struct FFMS_Track {
	typedef std::vector<TFrameInfo> frame_vec;
	frame_vec Frames;
	std::vector<size_t> InvisibleFrames;

	void MaybeReorderFrames();
	void MaybeHideFrames();

public:
	FFMS_TrackType TT;
	FFMS_TrackTimeBase TB;
	bool UseDTS;
	bool HasTS;

	void AddVideoFrame(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos = 0, unsigned int FrameSize = 0, bool Invisible = false);
	void AddAudioFrame(int64_t PTS, int64_t SampleStart, int64_t SampleCount, bool KeyFrame, int64_t FilePos = 0, unsigned int FrameSize = 0);

	void SortByPTS();

	int FindClosestVideoKeyFrame(int Frame) const;
	int FrameFromPTS(int64_t PTS) const;
	int FrameFromPos(int64_t Pos) const;
	int ClosestFrameFromPTS(int64_t PTS) const;
	int RealFrameNumber(int Frame) const;
	int VisibleFrameCount() const;

	void WriteTimecodes(const char *TimecodeFile) const;
	void Write(zipped_file &Stream) const;

	typedef frame_vec::allocator_type allocator_type;
	typedef frame_vec::size_type size_type;
	typedef frame_vec::difference_type difference_type;
	typedef frame_vec::const_pointer pointer;
	typedef frame_vec::const_reference reference;
	typedef frame_vec::value_type value_type;
	typedef frame_vec::const_iterator iterator;
	typedef frame_vec::const_reverse_iterator reverse_iterator;

	void clear() { Frames.clear(); InvisibleFrames.clear(); }

	bool empty() const { return Frames.empty(); }
	size_type size() const { return Frames.size(); }
	reference operator[](size_type pos) const { return Frames[pos]; }
	reference front() const { return Frames.front(); }
	reference back() const { return Frames.back(); }
	iterator begin() const { return Frames.begin(); }
	iterator end() const { return Frames.end(); }

	FFMS_Track();
	FFMS_Track(zipped_file &Stream);
	FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool UseDTS = false, bool HasTS = true);
};

struct FFMS_Index : public std::vector<FFMS_Track>, private noncopyable {
	int RefCount;
public:
	static void CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]);

	int AddRef();
	int Release();

	int Decoder;
	int ErrorHandling;
	int64_t Filesize;
	uint8_t Digest[20];

	void Sort();
	bool CompareFileSignature(const char *Filename);
	void WriteIndex(const char *IndexFile);

	FFMS_Index(const char *IndexFile);
	FFMS_Index(int64_t Filesize, uint8_t Digest[20], int Decoder, int ErrorHandling);
};

struct FFMS_Indexer : private noncopyable {
	std::map<int, FFMS_AudioProperties> LastAudioProperties;
protected:
	int IndexMask;
	int DumpMask;
	int ErrorHandling;
	TIndexCallback IC;
	void *ICPrivate;
	TAudioNameCallback ANC;
	void *ANCPrivate;
	std::string SourceFile;
	ScopedFrame DecodeFrame;

	int64_t Filesize;
	uint8_t Digest[20];

	void WriteAudio(SharedAudioContext &AudioContext, FFMS_Index *Index, int Track);
	void CheckAudioProperties(int Track, AVCodecContext *Context);
	int64_t IndexAudioPacket(int Track, AVPacket *Packet, SharedAudioContext &Context, FFMS_Index &TrackIndices);
	void ParseVideoPacket(SharedVideoContext &VideoContext, AVPacket &pkt, int *RepeatPict, int *FrameType, bool *Invisible);

public:
	static FFMS_Indexer *CreateIndexer(const char *Filename, FFMS_Sources Demuxer = FFMS_SOURCE_DEFAULT);
	FFMS_Indexer(const char *Filename);
	virtual ~FFMS_Indexer();
	void SetIndexMask(int IndexMask);
	void SetDumpMask(int DumpMask);
	void SetErrorHandling(int ErrorHandling);
	void SetProgressCallback(TIndexCallback IC, void *ICPrivate);
	void SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate);
	virtual FFMS_Index *DoIndexing() = 0;
	virtual int GetNumberOfTracks() = 0;
	virtual FFMS_TrackType GetTrackType(int Track) = 0;
	virtual const char *GetTrackCodec(int Track) = 0;
	virtual FFMS_Sources GetSourceType() = 0;
	virtual const char *GetFormatName() = 0;
};

class FFLAVFIndexer : public FFMS_Indexer {
	AVFormatContext *FormatContext;
	void ReadTS(const AVPacket &Packet, int64_t &TS, bool &UseDTS);
public:
	FFLAVFIndexer(const char *Filename, AVFormatContext *FormatContext);
	~FFLAVFIndexer();
	FFMS_Index *DoIndexing();
	int GetNumberOfTracks();
	FFMS_TrackType GetTrackType(int Track);
	const char *GetTrackCodec(int Track);
	const char *GetFormatName();
	FFMS_Sources GetSourceType();
};

class FFMatroskaIndexer : public FFMS_Indexer {
private:
	MatroskaFile *MF;
	MatroskaReaderContext MC;
	AVCodec *Codec[32];
public:
	FFMatroskaIndexer(const char *Filename);
	~FFMatroskaIndexer();
	FFMS_Index *DoIndexing();
	int GetNumberOfTracks();
	FFMS_TrackType GetTrackType(int Track);
	const char *GetTrackCodec(int Track);
	const char *GetFormatName();
	FFMS_Sources GetSourceType();
};

#ifdef HAALISOURCE

class FFHaaliIndexer : public FFMS_Indexer {
private:
	int SourceMode;
	CComPtr<IMMContainer> pMMC;
	int NumTracks;
	FFMS_TrackType TrackType[32];
	CComQIPtr<IPropertyBag> PropertyBags[32];
	int64_t Duration;
	AVFormatContext *FormatContext;
public:
	FFHaaliIndexer(const char *Filename, FFMS_Sources SourceMode);
	FFMS_Index *DoIndexing();
	int GetNumberOfTracks();
	FFMS_TrackType GetTrackType(int Track);
	const char *GetTrackCodec(int Track);
	const char *GetFormatName();
	FFMS_Sources GetSourceType();
};

#endif // HAALISOURCE

#endif
