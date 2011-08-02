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

#ifndef FFMSCOMPAT_H
#define	FFMSCOMPAT_H

#ifdef _WIN32
#	define snprintf _snprintf
#	ifdef __MINGW32__
#		define fseeko fseeko64
#		define ftello ftello64
#	else
#		define fseeko _fseeki64
#		define ftello _ftelli64
#	endif
#endif

// Compatibility with older/newer ffmpegs
#ifdef LIBAVFORMAT_VERSION_INT
#	if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(53,2,0))
#		define avformat_open_input(c,s,f,o) av_open_input_file(c,s,f,0,o) // this works because the parameters/options are not used
#	endif
#	if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(53,3,0))
#		define avformat_find_stream_info(c,o) av_find_stream_info(c)
#	endif
#endif

#ifdef LIBAVCODEC_VERSION_INT
#	if (LIBAVCODEC_VERSION_INT) >= (AV_VERSION_INT(52,94,3)) // there are ~3 revisions where this will break but fixing that is :effort:
#		undef SampleFormat
#	else
#		define AVSampleFormat SampleFormat
#		define av_get_bits_per_sample_fmt av_get_bits_per_sample_format
#		define AV_SAMPLE_FMT_U8		SAMPLE_FMT_U8
#		define AV_SAMPLE_FMT_S16	SAMPLE_FMT_S16
#		define AV_SAMPLE_FMT_S32	SAMPLE_FMT_S32
#		define AV_SAMPLE_FMT_FLT	SAMPLE_FMT_FLT
#		define AV_SAMPLE_FMT_DBL	SAMPLE_FMT_DBL
#	endif
#	if (LIBAVCODEC_VERSION_INT) < (AV_VERSION_INT(53,6,0))
#		define avcodec_open2(a,c,o) avcodec_open(a,c)
#		define avcodec_alloc_context3(c) avcodec_alloc_context()
#	endif
#endif

#ifdef LIBAVUTIL_VERSION_INT
#	if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(51, 1, 0))
#		define av_get_picture_type_char av_get_pict_type_char
#	endif
#endif

#endif // FFMSCOMPAT_H
