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

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libpostproc/postprocess.h>
#include <malloc.h>
#include "ff_filters.h"
#include "avs_common.h"

typedef struct
{
    AVS_FilterInfo *fi;
    pp_context *pp_ctx;
    pp_mode *pp_mode;
    struct SwsContext *sws_to_422p;
    struct SwsContext *sws_from_422p;
    AVPicture in_pic;
    AVPicture out_pic;
} ffpp_filter_t;

static void AVSC_CC free_filter( AVS_FilterInfo *fi )
{
    ffpp_filter_t *filter = fi->user_data;
    if( filter->pp_mode )
        pp_free_mode( filter->pp_mode );
    if( filter->pp_ctx )
        pp_free_context( filter->pp_ctx );
    if( filter->sws_to_422p )
        sws_freeContext( filter->sws_to_422p );
    if( filter->sws_from_422p )
        sws_freeContext( filter->sws_from_422p );
    avpicture_free( &filter->in_pic );
    avpicture_free( &filter->out_pic );
    free( filter );
}


static AVS_VideoFrame * AVSC_CC get_frame( AVS_FilterInfo *fi, int n )
{
    ffpp_filter_t *filter = fi->user_data;
    AVS_VideoFrame *src = ffms_avs_lib->avs_get_frame( fi->child, n );
    AVS_VideoFrame *dst = ffms_avs_lib->avs_new_video_frame_a( fi->env, &fi->vi, AVS_FRAME_ALIGN );
    uint8_t *src_data[3], *dst_data[3];
    int src_stride[3], dst_stride[3];
    fill_avs_frame_data( src, src_data, src_stride, 1, 0 );
    fill_avs_frame_data( dst, dst_data, dst_stride, 0, 0 );

    if( ffms_avs_lib->avs_is_yv12( &fi->vi ) )
        pp_postprocess( (const uint8_t**)src_data, src_stride, dst_data, dst_stride, fi->vi.width, fi->vi.height,
            NULL, 0, filter->pp_mode, filter->pp_ctx, 0 );
    else if ( avs_is_yuy2( &fi->vi ) )
    {
        src_data[1] = src_data[2] = dst_data[1] = dst_data[2] = NULL;
        src_stride[1] = src_stride[2] = dst_stride[1] = dst_stride[2] = 0;
        sws_scale( filter->sws_to_422p, (const uint8_t**)src_data, src_stride, 0, fi->vi.height, filter->in_pic.data, filter->in_pic.linesize );
        pp_postprocess( (const uint8_t**)filter->in_pic.data, filter->in_pic.linesize, filter->out_pic.data, filter->out_pic.linesize,
            fi->vi.width, fi->vi.height, NULL, 0, filter->pp_mode, filter->pp_ctx, 0 );
        sws_scale( filter->sws_from_422p, (const uint8_t**)filter->out_pic.data, filter->out_pic.linesize, 0, fi->vi.height, dst_data, dst_stride );
    }
    return dst;
}

AVS_Value FFPP_create( AVS_ScriptEnvironment *env, AVS_Value child, const char *pp )
{
    if( !avs_is_clip( child ) )
        return avs_new_value_error( "FFPP: No input provided" );
    ffpp_filter_t *filter = calloc( 1, sizeof(ffpp_filter_t) );
    if( !filter )
        return child;

    AVS_Clip *clip = ffms_avs_lib->avs_new_c_filter( env, &filter->fi, child, 1 );
    if( !clip )
    {
        free( filter );
        return child;
    }

    const AVS_VideoInfo *vi = ffms_avs_lib->avs_get_video_info( as_clip( child ) );
    filter->fi->vi = *vi;

    if( !vi->width || !vi->height )
        return avs_new_value_error( "FFPP: Input clip has no video" );

    int len = strlen( pp );
    char pp_workaround[ len+2 ]; // c99 (actual length + ',' + '\0')
    strcpy( pp_workaround, pp );
    strcat( pp_workaround, "," );

    filter->pp_mode = pp_get_mode_by_name_and_quality( pp_workaround, PP_QUALITY_MAX );
    if( !filter->pp_mode )
        return avs_new_value_error( "FFPP: Invalid postprocesing settings" );

    long avs_flags = ffms_avs_lib->avs_get_cpu_flags( env );
    int ppflags = avs_to_pp_cpu_flags( avs_flags );
    int64_t swsflags = avs_to_sws_cpu_flags( avs_flags ) | SWS_BICUBIC;

    if( ffms_avs_lib->avs_is_yv12( vi ) )
        ppflags |= PP_FORMAT_420;
    else if( avs_is_yuy2( vi ) )
    {
        ppflags |= PP_FORMAT_422;
        int ok = 1;
        filter->sws_to_422p = ffms_sws_get_context( vi->width, vi->height, PIX_FMT_YUYV422,
            vi->width, vi->height, PIX_FMT_YUV422P, swsflags, -1 );
        filter->sws_from_422p = ffms_sws_get_context( vi->width, vi->height, PIX_FMT_YUV422P,
            vi->width, vi->height, PIX_FMT_YUYV422, swsflags, -1 );
        ok &= !avpicture_alloc( &filter->in_pic, PIX_FMT_YUV422P, vi->width, vi->height );
        ok &= !avpicture_alloc( &filter->out_pic, PIX_FMT_YUV422P, vi->width, vi->height );
        ok &= filter->sws_to_422p && filter->sws_from_422p;
        if( !ok )
            return avs_new_value_error( "FFPP: Failure initializing YUY2 mode" );
    }
    else
        return avs_new_value_error( "FFPP: Only YV12 and YUY2 video supported" );

    filter->pp_ctx = pp_get_context( vi->width, vi->height, ppflags );
    if( !filter->pp_ctx )
        return avs_new_value_error( "FFPP: Failed to create context" );

    filter->fi->free_filter = free_filter;
    filter->fi->get_frame = get_frame;
    filter->fi->user_data = filter;
    return clip_val( clip );
}
