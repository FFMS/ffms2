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

#ifndef FFAVS_FILTERS_H
#define FFAVS_FILTERS_H

#include "avs_common.h"
#include "ffms.h"

AVS_Value FFVideoSource_create( AVS_ScriptEnvironment *env, const char *src, int track,
    FFMS_Index *index, int fps_num, int fps_den, const char *pp, int threads, int seek_mode,
    int rff_mode, int width, int height, const char *resizer_name, const char *csp_name,
    const char *var_prefix );

AVS_Value FFAudioSource_create( AVS_ScriptEnvironment *env, const char *src, int track,
    FFMS_Index *index, int adjust_delay, const char *var_prefix );

#ifdef FFMS_USE_POSTPROC
AVS_Value FFPP_create( AVS_ScriptEnvironment *env, AVS_Value child, const char *pp );
#endif

AVS_Value FFSWScale_create( AVS_ScriptEnvironment *env, AVS_Value child, int dst_width,
    int dst_height, const char *resizer_name, const char *csp_name );

#endif
