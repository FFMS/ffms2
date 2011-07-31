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
#include "ff_filters.h"

typedef struct
{
    AVS_FilterInfo *fi;
    FFMS_AudioSource *aud;
} ffaudiosource_filter_t;

static void AVSC_CC free_filter( AVS_FilterInfo *fi )
{
    ffaudiosource_filter_t *filter = fi->user_data;
    FFMS_DestroyAudioSource( filter->aud );
    free( filter );
}

static AVS_VideoFrame * AVSC_CC get_frame( AVS_FilterInfo *fi, int n )
{
    return NULL;
}

static int AVSC_CC get_parity( AVS_FilterInfo *fi, int n )
{
    return 0;
}

static int AVSC_CC get_audio( AVS_FilterInfo *fi, void *buf, INT64 start, INT64 count )
{
    ffaudiosource_filter_t *filter = fi->user_data;
    init_ErrorInfo( ei );

    if( FFMS_GetAudio( filter->aud, buf, start, count, &ei ) )
        fi->error = ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer );
    return 0;
}

static int AVSC_CC set_cache_hints( AVS_FilterInfo *fi, int cachehints, int frame_range )
{
    return 0;
}

AVS_Value FFAudioSource_create( AVS_ScriptEnvironment *env, const char *src, int track,
    FFMS_Index *index, int adjust_delay )
{
    ffaudiosource_filter_t *filter = calloc( 1, sizeof(ffaudiosource_filter_t) );
    if( !filter )
        return avs_void;

    AVS_Clip *clip = ffms_avs_lib->avs_new_c_filter( env, &filter->fi, avs_void, 0 );
    if( !clip )
    {
        free( filter );
        return avs_void;
    }
    memset( &filter->fi->vi, 0, sizeof(AVS_VideoInfo) );

    init_ErrorInfo( ei );

    filter->aud = FFMS_CreateAudioSource( src, track, index, adjust_delay, &ei );
    if( !filter->aud )
        return avs_new_value_error( ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer ) );

    const FFMS_AudioProperties *audp = FFMS_GetAudioProperties( filter->aud );
    filter->fi->vi.nchannels = audp->Channels;
    filter->fi->vi.num_audio_samples = audp->NumSamples;
    filter->fi->vi.audio_samples_per_second = audp->SampleRate;

    switch( audp->SampleFormat )
    {
        case FFMS_FMT_U8:  filter->fi->vi.sample_type = AVS_SAMPLE_INT8;  break;
        case FFMS_FMT_S16: filter->fi->vi.sample_type = AVS_SAMPLE_INT16; break;
        case FFMS_FMT_S32: filter->fi->vi.sample_type = AVS_SAMPLE_INT32; break;
        case FFMS_FMT_FLT: filter->fi->vi.sample_type = AVS_SAMPLE_FLOAT; break;
        default: return avs_new_value_error( "FFAudioSource: Invalid audio format" );
    }

    filter->fi->free_filter = free_filter;
    filter->fi->get_frame = get_frame;
    filter->fi->set_cache_hints = set_cache_hints;
    filter->fi->get_audio = get_audio;
    filter->fi->get_parity = get_parity;
    filter->fi->user_data = filter;
    return clip_val( clip );
}
