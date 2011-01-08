//  Copyright (c) 2010 FFmpegSource Project
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

#include <libswscale/swscale.h>
#include <malloc.h>
#include "ff_filters.h"
#include "avs_common.h"
#include "avs_utils.h"

typedef struct
{
    AVS_FilterInfo *fi;
    struct SwsContext *context;
    int orig_width;
    int orig_height;
    int flip_output;
} ffswscale_filter_t;

static void AVSC_CC free_filter( AVS_FilterInfo *fi )
{
    ffswscale_filter_t *filter = fi->user_data;
    sws_freeContext( filter->context );
    free( filter );
    fi->user_data = 0;
}

static AVS_VideoFrame * AVSC_CC get_frame( AVS_FilterInfo *fi, int n )
{
    ffswscale_filter_t *filter = fi->user_data;
    AVS_VideoFrame *src = ffms_avs_lib->avs_get_frame( fi->child, n );
    AVS_VideoFrame *dst = ffms_avs_lib->avs_new_video_frame_a( fi->env, &fi->vi, AVS_FRAME_ALIGN );

    uint8_t *src_data[3], *dst_data[3];
    int src_stride[3], dst_stride[3];
    fill_avs_frame_data( src, src_data, src_stride, 1, 0 );
    fill_avs_frame_data( dst, dst_data, dst_stride, 0, filter->flip_output );

    sws_scale( filter->context, (const uint8_t**)src_data, src_stride, 0, filter->orig_height, dst_data, dst_stride );
    ffms_avs_lib->avs_release_video_frame( src );
    return dst;
}

AVS_Value FFSWScale_create( AVS_ScriptEnvironment *env, AVS_Value child, int dst_width,
    int dst_height, const char *resizer_name, const char *csp_name )
{
    if( !avs_is_clip( child ) )
        return avs_new_value_error( "SWScale: No input provided" );

    ffswscale_filter_t *filter = malloc( sizeof(ffswscale_filter_t) );
    if( !filter )
        return child;

    AVS_Clip *clip = ffms_avs_lib->avs_new_c_filter( env, &filter->fi, child, 1 );
    if( !clip )
    {
        free( filter );
        return child;
    }

    const AVS_VideoInfo *vi = ffms_avs_lib->avs_get_video_info( as_clip( child ) );
    if( !vi->width || !vi->height )
        return avs_new_value_error( "SWScale: Input clip has no video" );
    filter->fi->vi = *vi;
    filter->orig_width  = vi->width;
    filter->orig_height = vi->height;
    filter->flip_output = avs_is_yuv( vi );

    enum PixelFormat src_format = vi_to_pix_fmt( vi );
    if( src_format == PIX_FMT_NONE )
        return avs_new_value_error( "SWScale: Unknown input clip colorspace" );

    filter->fi->vi.width  = dst_width  > 0 ? dst_width  : filter->orig_width;
    filter->fi->vi.height = dst_height > 0 ? dst_height : filter->orig_height;

    enum PixelFormat dst_pix_fmt = csp_name_to_pix_fmt( csp_name, src_format );
    if( dst_pix_fmt == PIX_FMT_NONE )
        return avs_new_value_error( ffms_avs_sprintf( "SWScale: Invalid colorspace specified (%s)", csp_name ) );

    switch( dst_pix_fmt )
    {
        case PIX_FMT_YUV420P: filter->fi->vi.pixel_type = AVS_CS_I420;  break;
        case PIX_FMT_YUYV422: filter->fi->vi.pixel_type = AVS_CS_YUY2;  break;
        case PIX_FMT_BGR24:   filter->fi->vi.pixel_type = AVS_CS_BGR24; break;
        case PIX_FMT_BGRA:    filter->fi->vi.pixel_type = AVS_CS_BGR32; break;
        default: filter->fi->vi.pixel_type = AVS_CS_UNKNOWN; break; // shutup gcc
    }

    filter->flip_output ^= avs_is_yuv( &filter->fi->vi );
    int resizer = resizer_name_to_swscale_name( resizer_name );
    if( !resizer )
        return avs_new_value_error( ffms_avs_sprintf( "SWScale: Invalid resizer specified (%s)", resizer_name ) );

    if( dst_pix_fmt == PIX_FMT_YUV420P && (filter->fi->vi.height&1) )
        return avs_new_value_error( "SWScale: mod 2 output height required" );

    if( (dst_pix_fmt == PIX_FMT_YUV420P || dst_pix_fmt == PIX_FMT_YUYV422) && (filter->fi->vi.width&1) )
        return avs_new_value_error( "SWScale: mod 2 output width required" );

    filter->context = ffms_sws_get_context( filter->orig_width, filter->orig_height, src_format, filter->fi->vi.width, filter->fi->vi.height,
         dst_pix_fmt, avs_to_ff_cpu_flags( ffms_avs_lib->avs_get_cpu_flags( env ), 0 ) | resizer, -1 );
    if( !filter->context )
    {
        free( filter );
        return avs_new_value_error( "SWScale: Context creation failed" );
    }
    filter->fi->user_data = filter;
    filter->fi->free_filter = free_filter;
    filter->fi->get_frame = get_frame;
    return clip_val( clip );
}
