//  Copyright (c) 2010-2011 FFmpegSource Project
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

#include "avs_common.h"
#include <string.h>
#include <stdio.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <ffms.h>

enum AVPixelFormat csp_name_to_pix_fmt_25( const char *csp_name, enum AVPixelFormat def )
{
    if( !csp_name || !strcmp( csp_name, "" ) )
        return def;
    if( !stricmp( csp_name, "YV12" ) )
        return PIX_FMT_YUV420P;
    if( !stricmp( csp_name, "YUY2" ) )
        return PIX_FMT_YUYV422;
    if( !stricmp( csp_name, "RGB24" ) )
        return PIX_FMT_BGR24;
    if( !stricmp( csp_name, "RGB32" ) )
        return PIX_FMT_BGRA;
    return PIX_FMT_NONE;
}

enum AVPixelFormat csp_name_to_pix_fmt_26( const char *csp_name, enum AVPixelFormat def )
{
    enum PixelFormat ret = csp_name_to_pix_fmt_25( csp_name, def );
    if( ret != PIX_FMT_NONE )
        return ret;
    else if( !stricmp( csp_name, "YV16" ) )
        return PIX_FMT_YUV422P;
    else if( !stricmp( csp_name, "YV24" ) )
        return PIX_FMT_YUV444P;
    else if( !stricmp( csp_name, "Y8" ) )
        return PIX_FMT_GRAY8;
    else if( !stricmp( csp_name, "YV411" ) )
        return PIX_FMT_YUV411P;
    return PIX_FMT_NONE;
}

enum AVPixelFormat vi_to_pix_fmt_25( const AVS_VideoInfo *vi )
{
    // so i can still use this entire function in the 2.6 version
    if( ffms_avs_lib->avs_is_yv12( vi ) )
        return PIX_FMT_YUV420P;
    else if( avs_is_yuy2( vi ) )
        return PIX_FMT_YUYV422;
    else if( avs_is_rgb24( vi ) )
        return PIX_FMT_BGR24;
    else if( avs_is_rgb32( vi ) )
        return PIX_FMT_BGRA;
    else
        return PIX_FMT_NONE;
}

enum AVPixelFormat vi_to_pix_fmt_26( const AVS_VideoInfo *vi )
{
    enum PixelFormat ret = vi_to_pix_fmt_25( vi );
    if( ret != PIX_FMT_NONE )
        return ret;
    else if( avs_is_yv16( vi ) )
        return PIX_FMT_YUV422P;
    else if( avs_is_yv24( vi ) )
        return PIX_FMT_YUV444P;
    else if( avs_is_y8( vi ) )
        return PIX_FMT_GRAY8;
    else if( avs_is_yv411( vi ) )
        return PIX_FMT_YUV411P;
    else
        return PIX_FMT_NONE;
}

int resizer_name_to_swscale_name( const char *resizer )
{
    if( !stricmp( resizer, "FAST_BILINEAR" ) )
        return SWS_FAST_BILINEAR;
    if( !stricmp( resizer, "BILINEAR" ) )
        return SWS_BILINEAR;
    if( !stricmp( resizer, "BICUBIC" ) )
        return SWS_BICUBIC;
    if( !stricmp( resizer, "X" ) )
        return SWS_X;
    if( !stricmp( resizer, "POINT" ) )
        return SWS_POINT;
    if( !stricmp( resizer, "AREA" ) )
        return SWS_AREA;
    if( !stricmp( resizer, "BICUBLIN" ) )
        return SWS_BICUBLIN;
    if( !stricmp( resizer, "GAUSS" ) )
        return SWS_GAUSS;
    if( !stricmp( resizer, "SINC" ) )
        return SWS_SINC;
    if( !stricmp( resizer, "LANCZOS" ) )
        return SWS_LANCZOS;
    if( !stricmp( resizer, "SPLINE" ) )
        return SWS_SPLINE;
    return 0;
}

void fill_avs_frame_data( AVS_VideoFrame *frm, uint8_t *ptr[3], int stride[3], char read, char vertical_flip )
{
    const static int plane[3] = { AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V };
    uint8_t *(*p_get_ptr)( AVS_VideoFrame *frm, int plane );
    p_get_ptr = read ? avs_get_read_ptr_p : avs_get_write_ptr_p; /* this causes a compiler warning - ignore it */
    for( int i = 0; i < 3; i++ )
    {
        if( vertical_flip )
        {
            stride[i] = - avs_get_pitch_p( frm, plane[i] );
            ptr[i] = p_get_ptr( frm, plane[i] )
                 - stride[i] * (ffms_avs_lib->avs_get_height_p( frm, plane[i] ) - 1);
        }
        else
        {
            ptr[i] = p_get_ptr( frm, plane[i] );
            stride[i] = avs_get_pitch_p( frm, plane[i] );
        }
    }
}

char *ffms_avs_sprintf( const char *format, ... )
{
    char buf[512]; /* effective message max length */
    va_list list;
    va_start( list, format );
    vsnprintf( buf, sizeof(buf), format, list );
    va_end( list );
    return strdup( buf );
}

char *ffms_avs_sprintf2( char *buf, size_t buf_len, const char *format, ... )
{
    va_list list;
    va_start( list, format );
    vsnprintf( buf, buf_len, format, list );
    va_end( list );
    return buf;
}

struct SwsContext *ffms_sws_get_context( int src_width, int src_height, int src_pix_fmt, int src_csp, int src_crange,
                                         int dst_width, int dst_height, int dst_pix_fmt, int dst_csp, int dst_crange, int64_t flags )
{
    struct SwsContext *ctx = sws_alloc_context();
    if( !ctx )
        return NULL;

    // The intention here is to never change the color range.
    // 0 = limited range, 1 = full range
    int src_range = src_crange == FFMS_CR_JPEG;
    int dst_range = dst_crange == FFMS_CR_JPEG;

    av_opt_set_int( ctx, "sws_flags", flags, 0 );
    av_opt_set_int( ctx, "srcw", src_width, 0 );
    av_opt_set_int( ctx, "srch", src_height, 0 );
    av_opt_set_int( ctx, "dstw", dst_width, 0 );
    av_opt_set_int( ctx, "dsth", dst_height, 0 );
    av_opt_set_int( ctx, "src_range", src_range, 0 );
    av_opt_set_int( ctx, "dst_range", dst_range, 0 );
    av_opt_set_int( ctx, "src_format", src_pix_fmt, 0 );
    av_opt_set_int( ctx, "dst_format", dst_pix_fmt, 0 );

    sws_setColorspaceDetails( ctx,
                              sws_getCoefficients( src_csp ), src_range,
                              sws_getCoefficients( dst_csp ), dst_range,
                              0, 1<<16, 1<<16 );
    if( sws_init_context( ctx, NULL, NULL ) < 0 )
    {
        sws_freeContext( ctx );
        return NULL;
    }

    return ctx;
}

int get_sws_assumed_color_space( int width, int height )
{
    return (width > 1024 || height >= 600) ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
}
