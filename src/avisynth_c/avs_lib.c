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

/* here we handle initialization and cleanup of our globally visible ffms_avs_lib.
 * this causes the situation where we need to handle intermixed/multiple load/frees.
 * to handle this, we use a ref variable to track how many instances of the library
 * are being handled concurrently and do no real work when the reference count is >1 */

#include <malloc.h>
#include "avs_common.h"

ffms_avs_lib_t ffms_avs_lib;
static volatile LONG ref = 0;

int ffms_load_avs_lib( AVS_ScriptEnvironment *env )
{
    if( InterlockedIncrement( &ref ) > 1 ) /* already initted - exit */
        return 0;
    ffms_avs_lib.library = LoadLibrary("avisynth");
    if (!ffms_avs_lib.library)
        return -1;

#define LOAD_AVS_FUNC(name, continue_on_fail)                  \
    ffms_avs_lib.name =                                        \
         (void *)GetProcAddress(ffms_avs_lib.library, #name ); \
    if( !continue_on_fail && !ffms_avs_lib.name )              \
        goto fail;
    OutputDebugString( "FFMS2 avs plugin: Initializing..." );

    LOAD_AVS_FUNC( avs_add_function, 0 );
    LOAD_AVS_FUNC( avs_at_exit, 0 );
    LOAD_AVS_FUNC( avs_bit_blt, 0 );
    LOAD_AVS_FUNC( avs_check_version, 0 );
    LOAD_AVS_FUNC( avs_get_audio, 0 );
    LOAD_AVS_FUNC( avs_get_cpu_flags, 0 );
    LOAD_AVS_FUNC( avs_get_frame, 0 );
    LOAD_AVS_FUNC( avs_get_height_p, 1 );
    LOAD_AVS_FUNC( avs_get_pitch_p, 1 );
    LOAD_AVS_FUNC( avs_get_read_ptr_p, 1 );
    LOAD_AVS_FUNC( avs_get_row_size_p, 1 );
    LOAD_AVS_FUNC( avs_get_video_info, 0 );
    LOAD_AVS_FUNC( avs_get_write_ptr_p, 1 );
    LOAD_AVS_FUNC( avs_is_rgb48, 1 );
    LOAD_AVS_FUNC( avs_is_rgb64, 1 );
    LOAD_AVS_FUNC( avs_is_yv12, 1 );
    LOAD_AVS_FUNC( avs_is_yv16, 1 );
    LOAD_AVS_FUNC( avs_is_yv24, 1 );
    LOAD_AVS_FUNC( avs_is_yv411, 1 );
    LOAD_AVS_FUNC( avs_is_y8, 1 );
    LOAD_AVS_FUNC( avs_new_c_filter, 0 );
    LOAD_AVS_FUNC( avs_new_video_frame_a, 0 );
    LOAD_AVS_FUNC( avs_release_video_frame, 0 );
    LOAD_AVS_FUNC( avs_set_to_clip, 0 );
    LOAD_AVS_FUNC( avs_set_var, 0 );
    LOAD_AVS_FUNC( avs_set_global_var, 0 );
    LOAD_AVS_FUNC( avs_take_clip, 0 );

    ffms_avs_lib.env = env;

    ffms_avs_lib.csp_name_to_pix_fmt = csp_name_to_pix_fmt;
    ffms_avs_lib.vi_to_pix_fmt = vi_to_pix_fmt;

    return 0;
fail:
    ffms_free_avs_lib( NULL, NULL );
    return -1;
}

void AVSC_CC ffms_free_avs_lib( void *user_data, AVS_ScriptEnvironment *env )
{
    /* only free the memory if there are no more referencess */
    if( !InterlockedDecrement( &ref ) && ffms_avs_lib.library )
    {
        free( ffms_avs_lib.library );
        ffms_avs_lib.library = NULL;
    }
}
