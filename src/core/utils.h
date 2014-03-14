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

#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <sstream>
#include <fstream>
#include <cstdio>
#include "ffms.h"
#include "matroskaparser.h"

extern "C" {
#include "stdiostream.h"
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#ifdef WITH_AVRESAMPLE
#include <libavresample/avresample.h>
#endif
}

// must be included after ffmpeg headers
#include "ffmscompat.h"

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

#ifdef __MINGW32__
#include <ext/stdio_filebuf.h>
#endif

#define FFMS_GET_VECTOR_PTR(v) (((v).size() ? &(v)[0] : NULL))

const int64_t ffms_av_nopts_value = static_cast<int64_t>(1) << 63;

// used for matroska<->ffmpeg codec ID mapping to avoid Win32 dependency
typedef struct FFMS_BITMAPINFOHEADER {
	uint32_t      biSize;
	int32_t       biWidth;
	int32_t       biHeight;
	uint16_t      biPlanes;
	uint16_t      biBitCount;
	uint32_t      biCompression;
	uint32_t      biSizeImage;
	int32_t       biXPelsPerMeter;
	int32_t       biYPelsPerMeter;
	uint32_t      biClrUsed;
	uint32_t      biClrImportant;
} FFMS_BITMAPINFOHEADER;

class FFMS_Exception : public std::exception {
private:
	std::string _Message;
	int _ErrorType;
	int _SubType;
public:
	FFMS_Exception(int ErrorType, int SubType, const char *Message = "");
	FFMS_Exception(int ErrorType, int SubType, const std::string &Message);
	~FFMS_Exception() throw ();
	const std::string &GetErrorMessage() const;
	int CopyOut(FFMS_ErrorInfo *ErrorInfo) const;
};

template<class T>
class FFSourceResources {
private:
	T *_PrivClass;
	bool _Enabled;
	bool _Arg;
public:
	FFSourceResources(T *Target) : _PrivClass(Target), _Enabled(true), _Arg(false) {
	}

	~FFSourceResources() {
		if (_Enabled)
			_PrivClass->Free(_Arg);
	}

	void SetEnabled(bool Enabled) {
		_Enabled = Enabled;
	}

	void SetArg(bool Arg) {
		_Arg = Arg;
	}

	void CloseCodec(bool Arg) {
		_Arg = Arg;
	}
};
// auto_ptr-ish holder for AVCodecContexts with overridable deleter
class FFCodecContext {
	AVCodecContext *CodecContext;
	void (*Deleter)(AVCodecContext *);
public:
	FFCodecContext() : CodecContext(0), Deleter(0) { }
	FFCodecContext(FFCodecContext &r) : CodecContext(r.CodecContext), Deleter(r.Deleter) { r.CodecContext = 0; }
	FFCodecContext(AVCodecContext *c, void (*d)(AVCodecContext *)) : CodecContext(c), Deleter(d) { }
	FFCodecContext& operator=(FFCodecContext r) { reset(r.CodecContext, r.Deleter); r.CodecContext = 0; return *this; }
	~FFCodecContext() { reset(); }
	AVCodecContext* operator->() { return CodecContext; }
	operator AVCodecContext*() { return CodecContext; }
	void reset(AVCodecContext *c = 0, void (*d)(AVCodecContext *) = 0) {
		if (CodecContext && Deleter) Deleter(CodecContext);
		CodecContext = c;
		Deleter = d;
	}
};

template<typename T, T *(*Alloc)(), void (*Del)(T **)>
class unknown_size {
	T *ptr;

	unknown_size(unknown_size const&);
	unknown_size& operator=(unknown_size const&);
public:
	operator T*() const { return ptr; }
	operator void*() const { return ptr; }
	T *operator->() const { return ptr; }
	void swap(unknown_size<T, Alloc, Del>& other) { std::swap(ptr, other.ptr); }

	unknown_size() : ptr(Alloc()) { }
	~unknown_size() { Del(&ptr); }
};

class ScopedFrame : public unknown_size<AVFrame, av_frame_alloc, av_frame_free> {
public:
	void reset() {
		av_frame_unref(*this);
	}
};

#ifdef WITH_AVRESAMPLE
typedef unknown_size<AVAudioResampleContext, avresample_alloc_context, avresample_free> FFResampleContext;
#else
typedef struct {} FFResampleContext;
#endif

inline void DeleteHaaliCodecContext(AVCodecContext *CodecContext) {
	av_freep(&CodecContext->extradata);
	av_freep(&CodecContext);
}
inline void DeleteMatroskaCodecContext(AVCodecContext *CodecContext) {
	avcodec_close(CodecContext);
	av_freep(&CodecContext);
}

class MatroskaReaderContext {
public:
	StdIoStream ST;
	uint8_t *Buffer;
	unsigned int BufferSize;
	char CSBuffer[4096];

	MatroskaReaderContext() {
		InitStdIoStream(&ST);
		Buffer = static_cast<uint8_t *>(av_mallocz(16384)); // arbitrarily decided number
		if (!Buffer)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED, "Out of memory");
		BufferSize = 16384;
	}

	~MatroskaReaderContext() {
		if (Buffer)
			av_free(Buffer);
		if (ST.fp) fclose(ST.fp);
	}
};

struct ffms_fstream : public std::fstream {
#ifdef __MINGW32__
private:
	__gnu_cxx::stdio_filebuf<char> filebuf;
public:
	bool is_open() const { return filebuf.is_open(); }
#endif
	ffms_fstream(const char *filename, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out);
};

class TrackCompressionContext {
public:
	CompressedStream *CS;
	unsigned CompressionMethod;
	void *CompressedPrivateData;
	unsigned CompressedPrivateDataSize;

	TrackCompressionContext(MatroskaFile *MF, TrackInfo *TI, unsigned int Track);
	~TrackCompressionContext();
};


void ClearErrorInfo(FFMS_ErrorInfo *ErrorInfo);
void ReadFrame(uint64_t FilePos, unsigned int &FrameSize, TrackCompressionContext *TCC, MatroskaReaderContext &Context);
bool AudioFMTIsFloat(AVSampleFormat FMT);
void InitNullPacket(AVPacket &pkt);
void FillAP(FFMS_AudioProperties &AP, AVCodecContext *CTX, FFMS_Track &Frames);

#ifdef HAALISOURCE
unsigned vtSize(VARIANT &vt);
void vtCopy(VARIANT& vt,void *dest);
FFCodecContext InitializeCodecContextFromHaaliInfo(CComQIPtr<IPropertyBag> pBag);
#endif

void InitializeCodecContextFromMatroskaTrackInfo(TrackInfo *TI, AVCodecContext *CodecContext);
FILE *ffms_fopen(const char *filename, const char *mode);
size_t ffms_mbstowcs (wchar_t *wcstr, const char *mbstr, size_t max);
#ifdef HAALISOURCE
CComPtr<IMMContainer> HaaliOpenFile(const char *SourceFile, FFMS_Sources SourceMode);
#endif // HAALISOURCE
void LAVFOpenFile(const char *SourceFile, AVFormatContext *&FormatContext);

void FlushBuffers(AVCodecContext *CodecContext);

namespace optdetail {
	template<typename T>
	T get_av_opt(void *v, const char *name) {
		int64_t value = 0;
		av_opt_get_int(v, name, 0, &value);
		return static_cast<T>(value);
	}

	template<>
	inline double get_av_opt<double>(void *v, const char *name) {
		double value = 0.0;
		av_opt_get_double(v, name, 0, &value);
		return value;
	}

	template<typename T>
	void set_av_opt(void *v, const char *name, T value) {
		av_opt_set_int(v, name, value, 0);
	}

	template<>
	inline void set_av_opt<double>(void *v, const char *name, double value) {
		av_opt_set_double(v, name, value, 0);
	}
}

template<typename FFMS_Struct>
class OptionMapper {
	struct OptionMapperBase {
		virtual void ToOpt(const FFMS_Struct *src, void *dst) const=0;
		virtual void FromOpt(FFMS_Struct *dst, void *src) const=0;
		virtual ~OptionMapperBase() {}
	};

	template<typename T>
	class OptionMapperImpl : public OptionMapperBase {
		T (FFMS_Struct::*ptr);
		const char *name;

	public:
		OptionMapperImpl(T (FFMS_Struct::*ptr), const char *name) : ptr(ptr), name(name) { }
		void ToOpt(const FFMS_Struct *src, void *dst) const { optdetail::set_av_opt(dst, name, src->*ptr); }
		void FromOpt(FFMS_Struct *dst, void *src) const { dst->*ptr = optdetail::get_av_opt<T>(src, name); }
	};

	OptionMapperBase *impl;

public:
	template<typename T>
	OptionMapper(const char *opt_name, T (FFMS_Struct::*member)) : impl(new OptionMapperImpl<T>(member, opt_name)) { }
	~OptionMapper() { delete impl; }

	void ToOpt(const FFMS_Struct *src, void *dst) const { impl->ToOpt(src, dst); }
	void FromOpt(FFMS_Struct *dst, void *src) const { impl->FromOpt(dst, src); }
};

template<typename T, int N>
T *ReadOptions(void *opt, OptionMapper<T> (&options)[N]) {
	T *ret = new T;
	for (int i = 0; i < N; ++i)
		options[i].FromOpt(ret, opt);
	return ret;
}

template<typename T, int N>
void SetOptions(const T* src, void *opt, OptionMapper<T> (&options)[N]) {
	for (int i = 0; i < N; ++i)
		options[i].ToOpt(src, opt);
}

#endif
