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
        return FFMS_PIX_FMT(YUV420P);
    if( !stricmp( csp_name, "YUY2" ) )
        return FFMS_PIX_FMT(YUYV422);
    if( !stricmp( csp_name, "RGB24" ) )
        return FFMS_PIX_FMT(BGR24);
    if( !stricmp( csp_name, "RGB32" ) )
        return FFMS_PIX_FMT(BGRA);
    if( !stricmp( csp_name, "YV16" ) )
        return FFMS_PIX_FMT(YUV422P);
    if( !stricmp( csp_name, "YV24" ) )
        return FFMS_PIX_FMT(YUV444P);
    if( !stricmp( csp_name, "Y8" ) )
        return FFMS_PIX_FMT(GRAY8);
    if( !stricmp( csp_name, "Y10" ) )
        return FFMS_PIX_FMT(GRAY10);
    if( !stricmp( csp_name, "Y12" ) )
        return FFMS_PIX_FMT(GRAY12);
//    if( !stricmp( csp_name, "Y14" ) )
//        return FFMS_PIX_FMT(GRAY14);
    if( !stricmp( csp_name, "Y16" ) )
        return FFMS_PIX_FMT(GRAY16);
    if( !stricmp( csp_name, "YV411" ) )
        return FFMS_PIX_FMT(YUV411P);
    if( !stricmp( csp_name, "RGB48" ) )
        return FFMS_PIX_FMT(BGR48);
    if( !stricmp( csp_name, "RGB64" ) )
        return FFMS_PIX_FMT(BGRA64);
    if( !stricmp( csp_name, "YUV420P10" ) )
        return FFMS_PIX_FMT(YUV420P10);
    if( !stricmp( csp_name, "YUV422P10" ) )
        return FFMS_PIX_FMT(YUV422P10);
    if( !stricmp( csp_name, "YUV444P10" ) )
        return FFMS_PIX_FMT(YUV444P10);
    if( !stricmp( csp_name, "YUV420P12" ) )
        return FFMS_PIX_FMT(YUV420P12);
    if( !stricmp( csp_name, "YUV422P12" ) )
        return FFMS_PIX_FMT(YUV422P12);
    if( !stricmp( csp_name, "YUV444P12" ) )
        return FFMS_PIX_FMT(YUV444P12);
    if( !stricmp( csp_name, "YUV420P14" ) )
        return FFMS_PIX_FMT(YUV420P14);
    if( !stricmp( csp_name, "YUV422P14" ) )
        return FFMS_PIX_FMT(YUV422P14);
    if( !stricmp( csp_name, "YUV444P14" ) )
        return FFMS_PIX_FMT(YUV444P14);
    if( !stricmp( csp_name, "YUV420P16" ) )
        return FFMS_PIX_FMT(YUV420P16);
    if( !stricmp( csp_name, "YUV422P16" ) )
        return FFMS_PIX_FMT(YUV422P16);
    if( !stricmp( csp_name, "YUV444P16" ) )
        return FFMS_PIX_FMT(YUV444P16);
    if( !stricmp( csp_name, "YUVA420P" ) )
        return FFMS_PIX_FMT(YUVA420P);
    if( !stricmp( csp_name, "YUVA422P" ) )
        return FFMS_PIX_FMT(YUVA422P);
    if( !stricmp( csp_name, "YUVA444P" ) )
        return FFMS_PIX_FMT(YUVA444P);
    if( !stricmp( csp_name, "YUVA420P10" ) )
        return FFMS_PIX_FMT(YUVA420P10);
    if( !stricmp( csp_name, "YUVA422P10" ) )
        return FFMS_PIX_FMT(YUVA422P10);
    if( !stricmp( csp_name, "YUVA444P10" ) )
        return FFMS_PIX_FMT(YUVA444P10);
//    if( !stricmp( csp_name, "YUVA420P12" ) )
//        return FFMS_PIX_FMT(YUVA420P12);
//    if( !stricmp( csp_name, "YUVA422P12" ) )
//        return FFMS_PIX_FMT(YUVA422P12);
//    if( !stricmp( csp_name, "YUVA444P12" ) )
//        return FFMS_PIX_FMT(YUVA444P12);
//    if( !stricmp( csp_name, "YUVA420P14" ) )
//        return FFMS_PIX_FMT(YUVA420P14);
//    if( !stricmp( csp_name, "YUVA422P14" ) )
//        return FFMS_PIX_FMT(YUVA422P14);
//    if( !stricmp( csp_name, "YUVA444P14" ) )
//        return FFMS_PIX_FMT(YUVA444P14);
    if( !stricmp( csp_name, "YUVA420P16" ) )
        return FFMS_PIX_FMT(YUVA420P16);
    if( !stricmp( csp_name, "YUVA422P16" ) )
        return FFMS_PIX_FMT(YUVA422P16);
    if( !stricmp( csp_name, "YUVA444P16" ) )
        return FFMS_PIX_FMT(YUVA444P16);
    if( !stricmp( csp_name, "RGBP" ) )
        return FFMS_PIX_FMT(GBRP);
    if( !stricmp( csp_name, "RGBP10" ) )
        return FFMS_PIX_FMT(GBRP10);
    if( !stricmp( csp_name, "RGBP12" ) )
        return FFMS_PIX_FMT(GBRP12);
    if( !stricmp( csp_name, "RGBP14" ) )
        return FFMS_PIX_FMT(GBRP14);
    if( !stricmp( csp_name, "RGBP16" ) )
        return FFMS_PIX_FMT(GBRP16);
    if( !stricmp( csp_name, "RGBAP" ) )
        return FFMS_PIX_FMT(GBRAP);
    if( !stricmp( csp_name, "RGBAP10" ) )
        return FFMS_PIX_FMT(GBRAP10);
    if( !stricmp( csp_name, "RGBAP12" ) )
        return FFMS_PIX_FMT(GBRAP12);
//    if( !stricmp( csp_name, "RGBAP14" ) )
//        return FFMS_PIX_FMT(GBRAP14);
    if( !stricmp( csp_name, "RGBAP16" ) )
        return FFMS_PIX_FMT(GBRAP16);
    return FFMS_PIX_FMT(NONE);
}

enum AVPixelFormat vi_to_pix_fmt( const AVS_VideoInfo *vi )
{
    if( ffms_avs_lib.avs_is_yv12( vi ) )
        return FFMS_PIX_FMT(YUV420P);
    else if( avs_is_yuy2( vi ) )
        return FFMS_PIX_FMT(YUYV422);
    else if( avs_is_rgb24( vi ) )
        return FFMS_PIX_FMT(BGR24);
    else if( avs_is_rgb32( vi ) )
        return FFMS_PIX_FMT(BGRA);
    else if( ffms_avs_lib.avs_is_yv16( vi ) )
        return FFMS_PIX_FMT(YUV422P);
    else if( ffms_avs_lib.avs_is_yv24( vi ) )
        return FFMS_PIX_FMT(YUV444P);
    else if( ffms_avs_lib.avs_is_y8( vi ) )
        return FFMS_PIX_FMT(GRAY8);
    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10)
        return FFMS_PIX_FMT(GRAY10);
    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12)
        return FFMS_PIX_FMT(GRAY12);
//    else if( ffms_avs_lib.avs_is_y( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14)
//        return FFMS_PIX_FMT(GRAY14);
    else if( ffms_avs_lib.avs_is_y16( vi ) )
        return FFMS_PIX_FMT(GRAY16);
    else if( ffms_avs_lib.avs_is_yv411( vi ) )
        return FFMS_PIX_FMT(YUV411P);
    else if( ffms_avs_lib.avs_is_rgb48( vi ) )
        return FFMS_PIX_FMT(BGR48);
    else if( ffms_avs_lib.avs_is_rgb64( vi ) )
        return FFMS_PIX_FMT(BGRA64);
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUV420P10);
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUV422P10);
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUV444P10);
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return FFMS_PIX_FMT(YUV420P12);
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return FFMS_PIX_FMT(YUV422P12);
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return FFMS_PIX_FMT(YUV444P12);
    else if( ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return FFMS_PIX_FMT(YUV420P14);
    else if( ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return FFMS_PIX_FMT(YUV422P14);
    else if( ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return FFMS_PIX_FMT(YUV444P14);
    else if( ffms_avs_lib.avs_is_yuv420p16( vi ) )
        return FFMS_PIX_FMT(YUV420P16);
    else if( ffms_avs_lib.avs_is_yuv422p16( vi ) )
        return FFMS_PIX_FMT(YUV422P16);
    else if( ffms_avs_lib.avs_is_yuv444p16( vi ) )
        return FFMS_PIX_FMT(YUV444P16);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) )
        return FFMS_PIX_FMT(YUVA420P);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) )
        return FFMS_PIX_FMT(YUVA422P);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) )
        return FFMS_PIX_FMT(YUVA444P);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUVA420P10);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUVA422P10);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(YUVA444P10);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return FFMS_PIX_FMT(YUVA420P12);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return FFMS_PIX_FMT(YUVA422P12);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
//        return FFMS_PIX_FMT(YUVA444P12);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return FFMS_PIX_FMT(YUVA420P14);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return FFMS_PIX_FMT(YUVA422P14);
//    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return FFMS_PIX_FMT(YUVA444P14);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv12( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return FFMS_PIX_FMT(YUVA420P16);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv16( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return FFMS_PIX_FMT(YUVA422P16);
    else if( ffms_avs_lib.avs_is_yuva( vi ) && ffms_avs_lib.avs_is_yv24( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return FFMS_PIX_FMT(YUVA444P16);
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) )
        return FFMS_PIX_FMT(GBRP);
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(GBRP10);
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return FFMS_PIX_FMT(GBRP12);
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
        return FFMS_PIX_FMT(GBRP14);
    else if( ffms_avs_lib.avs_is_planar_rgb( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return FFMS_PIX_FMT(GBRP16);
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) )
        return FFMS_PIX_FMT(GBRAP);
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 10 )
        return FFMS_PIX_FMT(GBRAP10);
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 12 )
        return FFMS_PIX_FMT(GBRAP12);
//    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 14 )
//        return FFMS_PIX_FMT(GBRAP14);
    else if( ffms_avs_lib.avs_is_planar_rgba( vi ) && ffms_avs_lib.avs_bits_per_pixel( vi ) == 16 )
        return FFMS_PIX_FMT(GBRAP16);
    else
        return FFMS_PIX_FMT(NONE);
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
