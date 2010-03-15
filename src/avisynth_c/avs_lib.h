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

#ifndef FFAVS_LIB_H
#define FFAVS_LIB_H

/* It's more convenient to dynamically load the avisynth interface at runtime than at
 * compile time, due to __stdcall and not actually having import libraries for different
 * platforms. So define function pointers for used functions and load them at runtime. */

typedef struct
{
    int (AVSC_CC *avs_add_function)(AVS_ScriptEnvironment *env, const char *name,
        const char *params, AVS_ApplyFunc apply, void *user_data);
    void (AVSC_CC *avs_at_exit)(AVS_ScriptEnvironment *env, AVS_ShutdownFunc function,
        void *user_data);
    void (AVSC_CC *avs_bit_blt)(AVS_ScriptEnvironment *, BYTE* dstp, int dst_pitch,
        const BYTE* srcp, int src_pitch, int row_size, int height);
    int (AVSC_CC *avs_get_audio)(AVS_Clip *, void *buf, INT64 start, INT64 count);
    long (AVSC_CC *avs_get_cpu_flags)(AVS_ScriptEnvironment *env);
    AVS_VideoFrame *(AVSC_CC *avs_get_frame)(AVS_Clip *, int n);
    const AVS_VideoInfo *(AVSC_CC *avs_get_video_info)(AVS_Clip *);
    AVS_Clip *(AVSC_CC *avs_new_c_filter)(AVS_ScriptEnvironment *e,
        AVS_FilterInfo **fi, AVS_Value child, int store_child);
    AVS_VideoFrame *(AVSC_CC *avs_new_video_frame_a)(AVS_ScriptEnvironment *,
        const AVS_VideoInfo * vi, int align);
    void (AVSC_CC *avs_release_video_frame)(AVS_VideoFrame *);
    void (AVSC_CC *avs_set_to_clip)(AVS_Value *, AVS_Clip *);
    int (AVSC_CC *avs_set_var)(AVS_ScriptEnvironment *, const char* name, AVS_Value val);
    AVS_Clip *(AVSC_CC *avs_take_clip)(AVS_Value val, AVS_ScriptEnvironment *env);

    AVS_ScriptEnvironment *env; /* the actual script environment */
} ffms_avs_lib_t;

/* it is highly convenient to have this library globally available to avoid having to pass it around */
extern       ffms_avs_lib_t *ffms_avs_lib;
int          ffms_load_avs_lib( AVS_ScriptEnvironment *env );
void AVSC_CC ffms_free_avs_lib( void *user_data, AVS_ScriptEnvironment *env );

#endif
