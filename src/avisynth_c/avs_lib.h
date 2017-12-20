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

#ifndef FFAVS_LIB_H
#define FFAVS_LIB_H

/* It's more convenient to dynamically load the avisynth interface at runtime than at
 * compile time, due to __stdcall and not actually having import libraries for different
 * platforms. So define function pointers for used functions and load them at runtime. */

enum AVPixelFormat; // predeclare this - to use it in the below prototypes

typedef struct ffms_avs_lib_t
{
    void *library;
#define AVSC_DECLARE_FUNC(name) name ## _func name
    AVSC_DECLARE_FUNC( avs_add_function );
    AVSC_DECLARE_FUNC( avs_at_exit );
    AVSC_DECLARE_FUNC( avs_bit_blt );
    AVSC_DECLARE_FUNC( avs_bits_per_pixel );
    AVSC_DECLARE_FUNC( avs_check_version );
    AVSC_DECLARE_FUNC( avs_get_audio );
    AVSC_DECLARE_FUNC( avs_get_cpu_flags );
    AVSC_DECLARE_FUNC( avs_get_frame );
    AVSC_DECLARE_FUNC( avs_get_height_p );
    AVSC_DECLARE_FUNC( avs_get_pitch_p );
    AVSC_DECLARE_FUNC( avs_get_read_ptr_p );
    AVSC_DECLARE_FUNC( avs_get_row_size_p );
    AVSC_DECLARE_FUNC( avs_get_video_info );
    AVSC_DECLARE_FUNC( avs_get_write_ptr_p );
    AVSC_DECLARE_FUNC( avs_is_planar_rgb );
    AVSC_DECLARE_FUNC( avs_is_planar_rgba );
    AVSC_DECLARE_FUNC( avs_is_rgb48 );
    AVSC_DECLARE_FUNC( avs_is_rgb64 );
    AVSC_DECLARE_FUNC( avs_is_yuv420p16 );
    AVSC_DECLARE_FUNC( avs_is_yuv422p16 );
    AVSC_DECLARE_FUNC( avs_is_yuv444p16 );
    AVSC_DECLARE_FUNC( avs_is_yuva );
    AVSC_DECLARE_FUNC( avs_is_yv12 );
    AVSC_DECLARE_FUNC( avs_is_yv16 );
    AVSC_DECLARE_FUNC( avs_is_yv24 );
    AVSC_DECLARE_FUNC( avs_is_yv411 );
    AVSC_DECLARE_FUNC( avs_is_y );
    AVSC_DECLARE_FUNC( avs_is_y8 );
    AVSC_DECLARE_FUNC( avs_is_y16 );
    AVSC_DECLARE_FUNC( avs_new_c_filter );
    AVSC_DECLARE_FUNC( avs_new_video_frame_a );
    AVSC_DECLARE_FUNC( avs_release_video_frame );
    AVSC_DECLARE_FUNC( avs_set_to_clip );
    AVSC_DECLARE_FUNC( avs_set_var );
    AVSC_DECLARE_FUNC( avs_set_global_var );
    AVSC_DECLARE_FUNC( avs_take_clip );

    AVS_ScriptEnvironment *env; /* the actual script environment */

    enum AVPixelFormat (*csp_name_to_pix_fmt)( const char *csp_name, enum AVPixelFormat def );
    enum AVPixelFormat (*vi_to_pix_fmt)( const AVS_VideoInfo *vi );

} ffms_avs_lib_t;

/* it is highly convenient to have this library globally available to avoid having to pass it around */
extern       ffms_avs_lib_t ffms_avs_lib;
int          ffms_load_avs_lib( AVS_ScriptEnvironment *env );
void AVSC_CC ffms_free_avs_lib( void *user_data, AVS_ScriptEnvironment *env );

#endif
