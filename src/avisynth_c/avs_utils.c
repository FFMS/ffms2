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
#include <ffmscompat.h>

enum AVPixelFormat csp_name_to_pix_fmt( const char *csp_name, enum AVPixelFormat def )
{
    if( !csp_name || !strcmp( csp_name, "" ) )
        return def;
    if( !stricmp( csp_name, "YV12" ) )
        return AV_PIX_FMT_YUV420P;
    if( !stricmp( csp_name, "YUY2" ) )
        return AV_PIX_FMT_YUYV422;
    if( !stricmp( csp_name, "RGB24" ) )
        return AV_PIX_FMT_BGR24;
    if( !stricmp( csp_name, "RGB32" ) )
        return AV_PIX_FMT_BGRA;
    if( !stricmp( csp_name, "YV16" ) )
        return AV_PIX_FMT_YUV422P;
    if( !stricmp( csp_name, "YV24" ) )
        return AV_PIX_FMT_YUV444P;
    if( !stricmp( csp_name, "Y8" ) )
        return AV_PIX_FMT_GRAY8;
    if( !stricmp( csp_name, "Y10" ) )
        return AV_PIX_FMT_GRAY10;
    if( !stricmp( csp_name, "Y12" ) )
        return AV_PIX_FMT_GRAY12;
//    if( !stricmp( csp_name, "Y14" ) )
//        return AV_PIX_FMT_GRAY14;
    if( !stricmp( csp_name, "Y16" ) )
        return AV_PIX_FMT_GRAY16;
    if( !stricmp( csp_name, "YV411" ) )
        return AV_PIX_FMT_YUV411P;
    if( !stricmp( csp_name, "RGB48" ) )
        return AV_PIX_FMT_BGR48;
    if( !stricmp( csp_name, "RGB64" ) )
        return AV_PIX_FMT_BGRA64;
    if( !stricmp( csp_name, "YUV420P10" ) )
        return AV_PIX_FMT_YUV420P10;
    if( !stricmp( csp_name, "YUV422P10" ) )
        return AV_PIX_FMT_YUV422P10;
    if( !stricmp( csp_name, "YUV444P10" ) )
        return AV_PIX_FMT_YUV444P10;
    if( !stricmp( csp_name, "YUV420P12" ) )
        return AV_PIX_FMT_YUV420P12;
    if( !stricmp( csp_name, "YUV422P12" ) )
        return AV_PIX_FMT_YUV422P12;
    if( !stricmp( csp_name, "YUV444P12" ) )
        return AV_PIX_FMT_YUV444P12;
    if( !stricmp( csp_name, "YUV420P14" ) )
        return AV_PIX_FMT_YUV420P14;
    if( !stricmp( csp_name, "YUV422P14" ) )
        return AV_PIX_FMT_YUV422P14;
    if( !stricmp( csp_name, "YUV444P14" ) )
        return AV_PIX_FMT_YUV444P14;
    if( !stricmp( csp_name, "YUV420P16" ) )
        return AV_PIX_FMT_YUV420P16;
    if( !stricmp( csp_name, "YUV422P16" ) )
        return AV_PIX_FMT_YUV422P16;
    if( !stricmp( csp_name, "YUV444P16" ) )
        return AV_PIX_FMT_YUV444P16;
    if( !stricmp( csp_name, "YUVA420P" ) )
        return AV_PIX_FMT_YUVA420P;
    if( !stricmp( csp_name, "YUVA422P" ) )
        return AV_PIX_FMT_YUVA422P;
    if( !stricmp( csp_name, "YUVA444P" ) )
        return AV_PIX_FMT_YUVA444P;
    if( !stricmp( csp_name, "YUVA420P10" ) )
        return AV_PIX_FMT_YUVA420P10;
    if( !stricmp( csp_name, "YUVA422P10" ) )
        return AV_PIX_FMT_YUVA422P10;
    if( !stricmp( csp_name, "YUVA444P10" ) )
        return AV_PIX_FMT_YUVA444P10;
//    if( !stricmp( csp_name, "YUVA420P12" ) )
//        return AV_PIX_FMT_YUVA420P12;
//    if( !stricmp( csp_name, "YUVA422P12" ) )
//        return AV_PIX_FMT_YUVA422P12;
//    if( !stricmp( csp_name, "YUVA444P12" ) )
//        return AV_PIX_FMT_YUVA444P12;
//    if( !stricmp( csp_name, "YUVA420P14" ) )
//        return AV_PIX_FMT_YUVA420P14;
//    if( !stricmp( csp_name, "YUVA422P14" ) )
//        return AV_PIX_FMT_YUVA422P14;
//    if( !stricmp( csp_name, "YUVA444P14" ) )
//        return AV_PIX_FMT_YUVA444P14;
    if( !stricmp( csp_name, "YUVA420P16" ) )
        return AV_PIX_FMT_YUVA420P16;
    if( !stricmp( csp_name, "YUVA422P16" ) )
        return AV_PIX_FMT_YUVA422P16;
    if( !stricmp( csp_name, "YUVA444P16" ) )
        return AV_PIX_FMT_YUVA444P16;
    if( !stricmp( csp_name, "RGBP" ) )
        return AV_PIX_FMT_GBRP;
    if( !stricmp( csp_name, "RGBP10" ) )
        return AV_PIX_FMT_GBRP10;
    if( !stricmp( csp_name, "RGBP12" ) )
        return AV_PIX_FMT_GBRP12;
    if( !stricmp( csp_name, "RGBP14" ) )
        return AV_PIX_FMT_GBRP14;
    if( !stricmp( csp_name, "RGBP16" ) )
        return AV_PIX_FMT_GBRP16;
    if( !stricmp( csp_name, "RGBAP" ) )
        return AV_PIX_FMT_GBRAP;
    if( !stricmp( csp_name, "RGBAP10" ) )
        return AV_PIX_FMT_GBRAP10;
    if( !stricmp( csp_name, "RGBAP12" ) )
        return AV_PIX_FMT_GBRAP12;
//    if( !stricmp( csp_name, "RGBAP14" ) )
//        return AV_PIX_FMT_GBRAP14;
    if( !stricmp( csp_name, "RGBAP16" ) )
        return AV_PIX_FMT_GBRAP16;
    return AV_PIX_FMT_NONE;
}

enum AVPixelFormat vi_to_pix_fmt( const AVS_VideoInfo *vi )
{
    if( ffms_avs_lib.avs_is_yv12( vi ) )
        return AV_PIX_FMT_YUV420P;
    else if( avs_is_yuy2( vi ) )
        return AV_PIX_FMT_YUYV422;
    else if( avs_is_rgb24( vi ) )
        return AV_PIX_FMT_BGR24;
    else if( avs_is_rgb32( vi ) )
        return AV_PIX_FMT_BGRA;
    else if( ffms_avs_lib.avs_is_yv16( vi ) )
        return AV_PIX_FMT_YUV422P;
    else if( ffms_avs_lib.avs_is_yv24( vi ) )
        return AV_PIX_FMT_YUV444P;
    else if( ffms_avs_lib.avs_is_y8( vi ) )
        return AV_PIX_FMT_GRAY8;
    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10)
        return AV_PIX_FMT_GRAY10;
    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12)
        return AV_PIX_FMT_GRAY12;
//    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14)
//        return AV_PIX_FMT_GRAY14;
    else if( ffms_avs_lib.avs_is_y16( vi ) )
        return AV_PIX_FMT_GRAY16;
    else if( ffms_avs_lib.avs_is_yv411( vi ) )
        return AV_PIX_FMT_YUV411P;
    else if( ffms_avs_lib.avs_is_rgb48( vi ) )
        return AV_PIX_FMT_BGR48;
    else if( ffms_avs_lib.avs_is_rgb64( vi ) )
        return AV_PIX_FMT_BGRA64;
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUV420P10;
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUV422P10;
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUV444P10;
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return AV_PIX_FMT_YUV420P12;
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return AV_PIX_FMT_YUV422P12;
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return AV_PIX_FMT_YUV444P12;
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return AV_PIX_FMT_YUV420P14;
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return AV_PIX_FMT_YUV422P14;
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return AV_PIX_FMT_YUV444P14;
    else if( ffms_avs_lib.avs_is_yuv420p16( vi ) )
        return AV_PIX_FMT_YUV420P16;
    else if( ffms_avs_lib.avs_is_yuv422p16( vi ) )
        return AV_PIX_FMT_YUV422P16;
    else if( ffms_avs_lib.avs_is_yuv444p16( vi ) )
        return AV_PIX_FMT_YUV444P16;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) )
        return AV_PIX_FMT_YUVA420P;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) )
        return AV_PIX_FMT_YUVA422P;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) )
        return AV_PIX_FMT_YUVA444P;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUVA420P10;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUVA422P10;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_YUVA444P10;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return AV_PIX_FMT_YUVA420P12;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return AV_PIX_FMT_YUVA422P12;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return AV_PIX_FMT_YUVA444P12;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return AV_PIX_FMT_YUVA420P14;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return AV_PIX_FMT_YUVA422P14;
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return AV_PIX_FMT_YUVA444P14;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return AV_PIX_FMT_YUVA420P16;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return AV_PIX_FMT_YUVA422P16;
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return AV_PIX_FMT_YUVA444P16;
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) )
        return AV_PIX_FMT_GBRP;
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_GBRP10;
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return AV_PIX_FMT_GBRP12;
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return AV_PIX_FMT_GBRP14;
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return AV_PIX_FMT_GBRP16;
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) )
        return AV_PIX_FMT_GBRAP;
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return AV_PIX_FMT_GBRAP10;
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return AV_PIX_FMT_GBRAP12;
//    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return AV_PIX_FMT_GBRAP14;
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return AV_PIX_FMT_GBRAP16;
    else
        return AV_PIX_FMT_NONE;
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
    p_get_ptr = read ? ffms_avs_lib.avs_get_read_ptr_p : ffms_avs_lib.avs_get_write_ptr_p; /* this causes a compiler warning - ignore it */
    for( int i = 0; i < 3; i++ )
    {
        if( vertical_flip )
        {
            stride[i] = - ffms_avs_lib.avs_get_pitch_p( frm, plane[i] );
            ptr[i] = p_get_ptr( frm, plane[i] )
                 - stride[i] * (ffms_avs_lib.avs_get_height_p( frm, plane[i] ) - 1);
        }
        else
        {
            ptr[i] = p_get_ptr( frm, plane[i] );
            stride[i] = ffms_avs_lib.avs_get_pitch_p( frm, plane[i] );
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
