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

#ifdef __GNUC__
#	define ffms_used __attribute__((used))
#else
#	define ffms_used
#endif

// Defaults to libav compatibility, uncomment (when building with msvc) to force ffmpeg compatibility.
//#define FFMS_USE_FFMPEG_COMPAT

// Attempt to auto-detect whether or not we are using ffmpeg.  Newer versions of ffmpeg have their micro versions 100+
#if LIBAVFORMAT_VERSION_MICRO > 99 || LIBAVUTIL_VERSION_MICRO > 99 || LIBAVCODEC_VERSION_MICRO > 99 || LIBSWSCALE_VERSION_MICRO > 99
#	ifndef FFMS_USE_FFMPEG_COMPAT
#		define FFMS_USE_FFMPEG_COMPAT
#	endif
#endif

// Helper to handle checking for different versions in libav and ffmpeg
// First version is required libav versio, second is required ffmpeg version
#ifdef FFMS_USE_FFMPEG_COMPAT
#  define VERSION_CHECK(LIB, cmp, u1, u2, u3, major, minor, micro) ((LIB) cmp (AV_VERSION_INT(major, minor, micro)))
#else
#  define VERSION_CHECK(LIB, cmp, major, minor, micro, u1, u2, u3) ((LIB) cmp (AV_VERSION_INT(major, minor, micro)))
#endif

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR) && _MSC_VER < 1900
#	define snprintf _snprintf
#endif

// Compatibility with older/newer ffmpegs
#ifdef LIBAVFORMAT_VERSION_INT
#	if VERSION_CHECK(LIBAVFORMAT_VERSION_INT, <, 53, 17, 0, 53, 25, 0)
#		define avformat_close_input(c) av_close_input_file(*c)
#	endif
#	if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(54,2,0))
#		define AV_DISPOSITION_ATTACHED_PIC 0xBEEFFACE
#	endif
#endif

#ifdef LIBAVCODEC_VERSION_INT
#	undef SampleFormat
#	define FFMS_CALCULATE_DELAY (CodecContext->has_b_frames + (CodecContext->thread_count - 1))
#   if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 54, 25, 0, 54, 51, 100)
#		define FFMS_ID(x) (CODEC_ID_##x)
#		define FFMS_CodecID CodecID
#   else
#		define FFMS_ID(x) (AV_CODEC_ID_##x)
#		define FFMS_CodecID AVCodecID
#		undef CodecID
#   endif
#   if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 54, 28, 0, 54, 59, 100)
static void av_frame_free(AVFrame **frame) { av_freep(frame); }
#		define av_frame_unref avcodec_get_frame_defaults
#   elif VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 55, 28, 1, 55, 45, 101)
#		define av_frame_free avcodec_free_frame
#		define av_frame_unref avcodec_get_frame_defaults
#   endif
#	if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 57, 8, 0, 57, 12, 100)
#		define av_packet_unref av_free_packet
#	endif
#	if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 57, 14, 0, 57, 33, 100)
#		define FFMSCODEC codec
static ffms_used int make_context(AVCodecContext *dst, AVStream *src) { return avcodec_copy_context(dst, src->codec); }
#	else
#		define FFMSCODEC codecpar
static ffms_used int make_context(AVCodecContext *dst, AVStream *src) { return avcodec_parameters_to_context(dst, src->codecpar); }
#	endif
#endif

#ifdef LIBAVUTIL_VERSION_INT
#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 27, 0, 51, 46, 100)
#		define av_get_packed_sample_fmt(fmt) (fmt < AV_SAMPLE_FMT_U8P ? fmt : fmt - (AV_SAMPLE_FMT_U8P - AV_SAMPLE_FMT_U8))
#	endif
#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 44, 0, 51, 76, 100)
		// Needs to be included before the AVPixelFormat define
#		include <libavutil/pixdesc.h>
#	endif

#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 42, 0, 51, 74, 100)
#		define AVPixelFormat PixelFormat
#		define FFMS_PIX_FMT(x) PIX_FMT_##x
#	else
#		define FFMS_PIX_FMT(x) AV_PIX_FMT_##x
#	endif

#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 52, 11, 0, 52, 32, 100)
#		define FFMS_PIX_FMT_FLAG(x) PIX_FMT_##x
#	else
#		define FFMS_PIX_FMT_FLAG(x) AV_PIX_FMT_FLAG_##x
#	endif

#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 44, 0, 51, 76, 100)
static const AVPixFmtDescriptor *av_pix_fmt_desc_get(AVPixelFormat pix_fmt) {
	if (pix_fmt < 0 || pix_fmt >= FFMS_PIX_FMT(NB))
		return NULL;

	return &av_pix_fmt_descriptors[pix_fmt];
}
#	endif
#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 52, 9, 0, 52, 20, 100)
#		define av_frame_alloc avcodec_alloc_frame
#	endif

#	if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >, 55, 0, 0, 55, 0, 100) || defined(FF_API_PLUS1_MINUS1)
#		define FFMS_DEPTH(x) ((x).depth)
#	else
#		define FFMS_DEPTH(x) ((x).depth_minus1 + 1)
#	endif
#endif


#ifndef WITH_SWRESAMPLE
#define ffms_convert(AVAudioResampleContext, output, out_plane_size, byte_per_sample_src, out_samples, input, in_plane_size, byte_per_sample_target, in_samples) \
		avresample_convert(AVAudioResampleContext, output, out_plane_size*byte_per_sample_src, out_samples, input, in_plane_size*byte_per_sample_target, in_samples)
#define ffms_open_resampler(context) avresample_open(context)
#define FFMS_ResampleContext         AVAudioResampleContext
#define ffms_resample_alloc_context  avresample_alloc_context
#define ffms_resample_free           avresample_free
#else
#define ffms_convert(AVAudioResampleContext, output, out_plane_size, byte_per_sample_src, out_samples, input, in_plane_size, bps, in_samples) \
		swr_convert(AVAudioResampleContext, output, out_samples, (const uint8_t**) input, in_samples)
#define ffms_open_resampler(context) swr_init(context)
#define FFMS_ResampleContext         SwrContext
#define ffms_resample_alloc_context  swr_alloc
#define ffms_resample_free           swr_free
#endif

#endif // FFMSCOMPAT_H
