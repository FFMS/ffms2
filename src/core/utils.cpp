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

#include <string.h>
#include <errno.h>
#include <algorithm>

#include "utils.h"

#include "codectype.h"
#include "indexing.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <io.h>
#	include <fcntl.h>
extern "C" {
#	include <libavutil/avstring.h>
}
#endif // _WIN32

extern bool GlobalUseUTF8Paths;

FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const char *Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) {
}

FFMS_Exception::FFMS_Exception(int ErrorType, int SubType, const std::string &Message) : _Message(Message), _ErrorType(ErrorType), _SubType(SubType) {
}

FFMS_Exception::~FFMS_Exception() throw () {
}

const std::string &FFMS_Exception::GetErrorMessage() const {
	return _Message;
}

int FFMS_Exception::CopyOut(FFMS_ErrorInfo *ErrorInfo) const {
	if (ErrorInfo) {
		ErrorInfo->ErrorType = _ErrorType;
		ErrorInfo->SubType = _SubType;

		if (ErrorInfo->BufferSize > 0) {
			memset(ErrorInfo->Buffer, 0, ErrorInfo->BufferSize);
			_Message.copy(ErrorInfo->Buffer, ErrorInfo->BufferSize - 1);
		}
	}

	return (_ErrorType << 16) | _SubType;
}

TrackCompressionContext::TrackCompressionContext(MatroskaFile *MF, TrackInfo *TI, unsigned int Track) {
	CS = NULL;
	CompressedPrivateData = NULL;
	CompressedPrivateDataSize = 0;
	CompressionMethod = TI->CompMethod;

	if (CompressionMethod == COMP_ZLIB) {
		char ErrorMessage[512];
		CS = cs_Create(MF, Track, ErrorMessage, sizeof(ErrorMessage));
		if (CS == NULL) {
			std::ostringstream buf;
			buf << "Can't create MKV track decompressor: " << ErrorMessage;
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
		}
	} else if (CompressionMethod == COMP_PREPEND) {
		CompressedPrivateData		= TI->CompMethodPrivate;
		CompressedPrivateDataSize	= TI->CompMethodPrivateSize;
	} else {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Can't create MKV track decompressor: unknown or unsupported compression method");
	}
}

TrackCompressionContext::~TrackCompressionContext() {
	if (CS)
		cs_Destroy(CS);
}

void ClearErrorInfo(FFMS_ErrorInfo *ErrorInfo) {
	if (ErrorInfo) {
		ErrorInfo->ErrorType = FFMS_ERROR_SUCCESS;
		ErrorInfo->SubType = FFMS_ERROR_SUCCESS;

		if (ErrorInfo->BufferSize > 0)
			ErrorInfo->Buffer[0] = 0;
	}
}

template<class T> static void safe_aligned_reallocz(T *&ptr, size_t old_size, size_t new_size) {
	void *newalloc = av_mallocz(new_size);
	if (newalloc) {
		memcpy(newalloc, ptr, FFMIN(old_size, new_size));
	}
	av_free(ptr);
	ptr = static_cast<T*>(newalloc);
}

void ReadFrame(uint64_t FilePos, unsigned int &FrameSize, TrackCompressionContext *TCC, MatroskaReaderContext &Context) {
	memset(Context.Buffer, 0, Context.BufferSize); // necessary to avoid lavc hurfing a durf with some mpeg4 video streams
	if (TCC && TCC->CompressionMethod == COMP_ZLIB) {
		CompressedStream *CS = TCC->CS;
		unsigned int DecompressedFrameSize = 0;

		cs_NextFrame(CS, FilePos, FrameSize);

		for (;;) {
			int ReadBytes = cs_ReadData(CS, Context.CSBuffer, sizeof(Context.CSBuffer));
			if (ReadBytes < 0) {
				std::ostringstream buf;
				buf << "Error decompressing data: " << cs_GetLastError(CS);
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
			}

			if (ReadBytes == 0) {
				FrameSize = DecompressedFrameSize;
				return;
			}

			if (Context.BufferSize < DecompressedFrameSize + ReadBytes + FF_INPUT_BUFFER_PADDING_SIZE) {
				size_t NewSize = (DecompressedFrameSize + ReadBytes) * 2;
				safe_aligned_reallocz(Context.Buffer, Context.BufferSize, NewSize);
				if (Context.Buffer == NULL)
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
					"Out of memory");
				Context.BufferSize = NewSize;
			}

			memcpy(Context.Buffer + DecompressedFrameSize, Context.CSBuffer, ReadBytes);
			DecompressedFrameSize += ReadBytes;
		}
	} else {
		if (fseeko(Context.ST.fp, FilePos, SEEK_SET)) {
			std::ostringstream buf;
			buf << "fseek(): " << strerror(errno);
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_SEEKING, buf.str());
		}

		if (TCC && TCC->CompressionMethod == COMP_PREPEND) {
			unsigned ReqBufsize = FrameSize + TCC->CompressedPrivateDataSize + FF_INPUT_BUFFER_PADDING_SIZE;
			if (Context.BufferSize < ReqBufsize) {
				size_t NewSize = ReqBufsize * 2;
				safe_aligned_reallocz(Context.Buffer, Context.BufferSize, NewSize);
				if (Context.Buffer == NULL)
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED, "Out of memory");
				Context.BufferSize = NewSize;
			}

			/* // maybe faster? maybe not?
			for (int i=0; i < TCC->CompressedPrivateDataSize; i++)
				*(Context.Buffer)++ = ((uint8_t *)TCC->CompressedPrivateData)[i];
			*/
			// screw it, memcpy and fuck the losers who use header compression
			memcpy(Context.Buffer, TCC->CompressedPrivateData, TCC->CompressedPrivateDataSize);
		}
		else if (Context.BufferSize < FrameSize + FF_INPUT_BUFFER_PADDING_SIZE) {
			size_t NewSize = FrameSize * 2;
			safe_aligned_reallocz(Context.Buffer, Context.BufferSize, NewSize);
			if (Context.Buffer == NULL)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
					"Out of memory");
			Context.BufferSize = NewSize;
		}

		uint8_t *TargetPtr = Context.Buffer;
		if (TCC && TCC->CompressionMethod == COMP_PREPEND)
			TargetPtr += TCC->CompressedPrivateDataSize;

		size_t ReadBytes = fread(TargetPtr, 1, FrameSize, Context.ST.fp);
		if (ReadBytes != FrameSize) {
			if (ReadBytes == 0) {
				if (feof(Context.ST.fp)) {
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
						"Unexpected EOF while reading frame");
				} else {
					std::ostringstream buf;
					buf << "Error reading frame: " << strerror(errno);
					throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_SEEKING, buf.str());
				}
			} else {
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
					"Short read while reading frame");
			}
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				"Unknown read error");
		}

		if (TCC && TCC->CompressionMethod == COMP_PREPEND)
			FrameSize += TCC->CompressedPrivateDataSize;
		return;
	}
}

void InitNullPacket(AVPacket &pkt) {
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
}

extern "C" {
#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 52, 2, 0, 52, 6, 100)
#include <libavutil/channel_layout.h>
#elif VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 51, 26, 0, 51, 45, 100)
#include <libavutil/audioconvert.h>
#else
static int64_t av_get_default_channel_layout(int nb_channels) {
	switch(nb_channels) {
		case 1: return AV_CH_LAYOUT_MONO;
		case 2: return AV_CH_LAYOUT_STEREO;
		case 3: return AV_CH_LAYOUT_SURROUND;
		case 4: return AV_CH_LAYOUT_QUAD;
		case 5: return AV_CH_LAYOUT_5POINT0;
		case 6: return AV_CH_LAYOUT_5POINT1;
		case 7: return AV_CH_LAYOUT_6POINT1;
		case 8: return AV_CH_LAYOUT_7POINT1;
		default: return 0;
	}
}
#endif
}

void FillAP(FFMS_AudioProperties &AP, AVCodecContext *CTX, FFMS_Track &Frames) {
	AP.SampleFormat = static_cast<FFMS_SampleFormat>(av_get_packed_sample_fmt(CTX->sample_fmt));
	AP.BitsPerSample = av_get_bytes_per_sample(CTX->sample_fmt) * 8;
	AP.Channels = CTX->channels;
	AP.ChannelLayout = CTX->channel_layout;
	AP.SampleRate = CTX->sample_rate;
	if (!Frames.empty()) {
		AP.NumSamples = (Frames.back()).SampleStart + (Frames.back()).SampleCount;
		AP.FirstTime = ((Frames.front().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
		AP.LastTime = ((Frames.back().PTS * Frames.TB.Num) / (double)Frames.TB.Den) / 1000;
	}

	if (AP.ChannelLayout == 0)
		AP.ChannelLayout = av_get_default_channel_layout(AP.Channels);
}

#ifdef HAALISOURCE

unsigned vtSize(VARIANT &vt) {
	if (V_VT(&vt) != (VT_ARRAY | VT_UI1))
		return 0;
	long lb,ub;
	if (FAILED(SafeArrayGetLBound(V_ARRAY(&vt),1,&lb)) ||
		FAILED(SafeArrayGetUBound(V_ARRAY(&vt),1,&ub)))
		return 0;
	return ub - lb + 1;
}

void vtCopy(VARIANT& vt,void *dest) {
	unsigned sz = vtSize(vt);
	if (sz > 0) {
		void  *vp;
		if (SUCCEEDED(SafeArrayAccessData(V_ARRAY(&vt),&vp))) {
			memcpy(dest,vp,sz);
			SafeArrayUnaccessData(V_ARRAY(&vt));
		}
	}
}

#endif

void InitializeCodecContextFromMatroskaTrackInfo(TrackInfo *TI, AVCodecContext *CodecContext) {
	uint8_t *PrivateDataSrc = static_cast<uint8_t *>(TI->CodecPrivate);
	size_t PrivateDataSize = TI->CodecPrivateSize;
	size_t BIHSize = sizeof(FFMS_BITMAPINFOHEADER); // 40 bytes
	if (!strncmp(TI->CodecID, "V_MS/VFW/FOURCC", 15) && PrivateDataSize >= BIHSize) {
		// For some reason UTVideo requires CodecContext->codec_tag (i.e. the FourCC) to be set.
		// Fine, it can't hurt to set it, so let's go find it.
		// In a V_MS/VFW/FOURCC track, the codecprivate starts with a BITMAPINFOHEADER. If you treat that struct
		// as an array of uint32_t, the biCompression member (that's the FourCC) can be found at offset 4.
		// Therefore the following derp.
		CodecContext->codec_tag = reinterpret_cast<uint32_t *>(PrivateDataSrc)[4];

		// Now skip copying the BITMAPINFOHEADER into the extradata, because lavc doesn't expect it to be there.
		if (PrivateDataSize <= BIHSize) {
			PrivateDataSrc = NULL;
			PrivateDataSize = 0;
		}
		else {
			PrivateDataSrc += BIHSize;
			PrivateDataSize -= BIHSize;
		}
	}
	// I think you might need to do some special handling for A_MS/ACM extradata too,
	// but I don't think anyone actually uses that.

	if (PrivateDataSrc && PrivateDataSize > 0) {
		CodecContext->extradata = static_cast<uint8_t *>(av_mallocz(PrivateDataSize + FF_INPUT_BUFFER_PADDING_SIZE));
		CodecContext->extradata_size = PrivateDataSize;
		memcpy(CodecContext->extradata, PrivateDataSrc, PrivateDataSize);
	}

	if (TI->Type == TT_VIDEO) {
		CodecContext->coded_width = TI->AV.Video.PixelWidth;
		CodecContext->coded_height = TI->AV.Video.PixelHeight;
	} else if (TI->Type == TT_AUDIO) {
		CodecContext->sample_rate = mkv_TruncFloat(TI->AV.Audio.SamplingFreq);
		CodecContext->bits_per_coded_sample = TI->AV.Audio.BitDepth;
		CodecContext->channels = TI->AV.Audio.Channels;
	}
}

#ifdef HAALISOURCE

FFCodecContext InitializeCodecContextFromHaaliInfo(CComQIPtr<IPropertyBag> pBag) {
	CComVariant pV;
	if (FAILED(pBag->Read(L"Type", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
		return FFCodecContext();

	unsigned int TT = pV.uintVal;

	FFCodecContext CodecContext(avcodec_alloc_context3(NULL), DeleteHaaliCodecContext);

	unsigned int FourCC = 0;
	if (TT == TT_VIDEO) {
		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Video.PixelWidth", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			CodecContext->coded_width = pV.uintVal;

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Video.PixelHeight", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			CodecContext->coded_height = pV.uintVal;

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"FOURCC", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			FourCC = pV.uintVal;

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"CodecPrivate", &pV, NULL))) {
			CodecContext->extradata_size = vtSize(pV);
			CodecContext->extradata = static_cast<uint8_t*>(av_mallocz(CodecContext->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE));
			vtCopy(pV, CodecContext->extradata);
		}
	}
	else if (TT == TT_AUDIO) {
		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"CodecPrivate", &pV, NULL))) {
			CodecContext->extradata_size = vtSize(pV);
			CodecContext->extradata = static_cast<uint8_t*>(av_mallocz(CodecContext->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE));
			vtCopy(pV, CodecContext->extradata);
		}

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Audio.SamplingFreq", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			CodecContext->sample_rate = pV.uintVal;

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Audio.BitDepth", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			CodecContext->bits_per_coded_sample = pV.uintVal;

		pV.Clear();
		if (SUCCEEDED(pBag->Read(L"Audio.Channels", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_UI4)))
			CodecContext->channels = pV.uintVal;
	}

	pV.Clear();
	if (SUCCEEDED(pBag->Read(L"CodecID", &pV, NULL)) && SUCCEEDED(pV.ChangeType(VT_BSTR))) {
		char CodecStr[2048];
		wcstombs(CodecStr, pV.bstrVal, 2000);

		CodecContext->codec = avcodec_find_decoder(MatroskaToFFCodecID(CodecStr, CodecContext->extradata, FourCC, CodecContext->bits_per_coded_sample));
	}
	return CodecContext;
}

#endif


// All this filename chikanery that follows is supposed to make sure both local
// codepage (used by avisynth etc) and UTF8 (potentially used by API users) strings
// work correctly on Win32.
// It's a really ugly hack, and I blame Microsoft for it.
#ifdef _WIN32
static std::wstring char_to_wstring(const char *s, unsigned int cp) {
	std::wstring ret;
	std::vector<wchar_t> tmp;
	int len;
	if (!(len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1, NULL, 0)))
		return ret;

	tmp.resize(len);
	if (MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1 , &tmp[0], len) <= 0)
		return ret;

	ret.assign(&tmp[0]);
	return ret;
}

static std::wstring widen_path(const char *s) {
	return char_to_wstring(s, GlobalUseUTF8Paths ? CP_UTF8 : CP_ACP);
}
#endif

FILE *ffms_fopen(const char *filename, const char *mode) {
#ifdef _WIN32
	std::wstring filename_wide = widen_path(filename);
	std::wstring mode_wide     = widen_path(mode);
	if (filename_wide.size() && mode_wide.size())
		return _wfopen(filename_wide.c_str(), mode_wide.c_str());
	else
		return fopen(filename, mode);
#else
	return fopen(filename, mode);
#endif /* _WIN32 */
}

size_t ffms_mbstowcs(wchar_t *wcstr, const char *mbstr, size_t max) {
#ifdef _WIN32
	// this is only called by HaaliOpenFile anyway, so I think this is safe
	return static_cast<size_t>(MultiByteToWideChar((GlobalUseUTF8Paths ? CP_UTF8 : CP_ACP), MB_ERR_INVALID_CHARS, mbstr, -1, wcstr, max));
#else
	return mbstowcs(wcstr, mbstr, max);
#endif
}

#ifdef __MINGW32__
static int open_flags(std::ios::openmode mode) {
	int flags = 0;
	if ((mode & std::ios::in) && (mode & std::ios::out))
		flags = _O_CREAT | _O_TRUNC | _O_RDWR;
	if (mode & std::ios::in)
		flags = _O_RDONLY;
	else if (mode & std::ios::out)
		flags = _O_CREAT | _O_TRUNC | _O_WRONLY;

#ifdef _O_BINARY
	flags |= _O_BINARY;
#endif
	return flags;
}
#endif

// ffms_fstream stuff
ffms_fstream::ffms_fstream(const char *filename, std::ios_base::openmode mode)
#ifdef __MINGW32__
: filebuf(_wopen(widen_path(filename).c_str(), open_flags(mode), 0666), mode)
#endif
{
#if defined(_WIN32)

#ifndef __MINGW32__
	std::wstring filename_wide = widen_path(filename);
	if (filename_wide.size())
		open(filename_wide.c_str(), mode);
	else
		open(filename, mode);
#else
	// Unlike MSVC, mingw's iostream library doesn't have an fstream overload
	// that takes a wchar_t* filename, so instead we use gcc's nonstandard
	// fd wrapper
	std::iostream::rdbuf(&filebuf);
#endif //__MINGW32__

#else // _WIN32
	open(filename, mode);
#endif // _WIN32
}

// End of filename hackery.


#ifdef HAALISOURCE

CComPtr<IMMContainer> HaaliOpenFile(const char *SourceFile, FFMS_Sources SourceMode) {
	CComPtr<IMMContainer> pMMC;

	CLSID clsid = HAALI_MPEG_PARSER;
	if (SourceMode == FFMS_SOURCE_HAALIOGG)
		clsid = HAALI_OGG_PARSER;

	if (FAILED(pMMC.CoCreateInstance(clsid)))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
			"Can't create parser");

	CComPtr<IMemAlloc> pMA;
	if (FAILED(pMA.CoCreateInstance(CLSID_MemAlloc)))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
			"Can't create memory allocator");

	CComPtr<IMMStream> pMS;
	if (FAILED(pMS.CoCreateInstance(CLSID_DiskFile)))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
			"Can't create disk file reader");

	WCHAR WSourceFile[2048];
	ffms_mbstowcs(WSourceFile, SourceFile, 2000);
	CComQIPtr<IMMStreamOpen> pMSO(pMS);
	if (FAILED(pMSO->Open(WSourceFile)))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Can't open file");

	if (FAILED(pMMC->Open(pMS, 0, NULL, pMA))) {
		if (SourceMode == FFMS_SOURCE_HAALIMPEG)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_INVALID_ARGUMENT,
				"Can't parse file, most likely a transport stream not cut at packet boundaries");
		else
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_INVALID_ARGUMENT,
				"Can't parse file");
	}

	return pMMC;
}

#endif

void LAVFOpenFile(const char *SourceFile, AVFormatContext *&FormatContext) {
	if (avformat_open_input(&FormatContext, SourceFile, NULL, NULL) != 0)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Couldn't open '") + SourceFile + "'");

	if (avformat_find_stream_info(FormatContext,NULL) < 0) {
		avformat_close_input(&FormatContext);
		FormatContext = NULL;
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Couldn't find stream information");
	}
}

void FlushBuffers(AVCodecContext *CodecContext) {
	if (CodecContext->codec->flush)
		avcodec_flush_buffers(CodecContext);
	else {
		// If the codec doesn't have flush(), it might not need it... or it
		// might need it and just not implement it as in the case of VC-1, so
		// close and reopen the codec
		const AVCodec *codec = CodecContext->codec;
		avcodec_close(CodecContext);
		// Whether or not codec is const varies between versions
		avcodec_open2(CodecContext, const_cast<AVCodec *>(codec), 0);
	}
}
