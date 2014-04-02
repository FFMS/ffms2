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

#include "haalicommon.h"

#ifdef HAALISOURCE

#include "codectype.h"
#include "utils.h"

unsigned vtSize(VARIANT &vt) {
	if (V_VT(&vt) != (VT_ARRAY | VT_UI1))
		return 0;
	long lb, ub;
	if (FAILED(SafeArrayGetLBound(V_ARRAY(&vt), 1, &lb)) ||
		FAILED(SafeArrayGetUBound(V_ARRAY(&vt), 1, &ub)))
		return 0;
	return ub - lb + 1;
}

void vtCopy(VARIANT& vt, void *dest) {
	unsigned sz = vtSize(vt);
	if (sz > 0) {
		void  *vp;
		if (SUCCEEDED(SafeArrayAccessData(V_ARRAY(&vt), &vp))) {
			memcpy(dest, vp, sz);
			SafeArrayUnaccessData(V_ARRAY(&vt));
		}
	}
}

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

	std::wstring WSourceFile = widen_path(SourceFile);
	CComQIPtr<IMMStreamOpen> pMSO(pMS);
	if (FAILED(pMSO->Open(WSourceFile.c_str())))
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
