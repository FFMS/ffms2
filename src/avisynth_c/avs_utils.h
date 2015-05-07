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

#ifndef FFAVS_UTILS_H
#define FFAVS_UTILS_H

#include <stdint.h>

enum  AVPixelFormat csp_name_to_pix_fmt_25( const char *csp_name, enum AVPixelFormat def );
enum  AVPixelFormat csp_name_to_pix_fmt_26( const char *csp_name, enum AVPixelFormat def );
enum  AVPixelFormat vi_to_pix_fmt_25( const AVS_VideoInfo *vi );
enum  AVPixelFormat vi_to_pix_fmt_26( const AVS_VideoInfo *vi );
int   resizer_name_to_swscale_name( const char *resizer );
void  fill_avs_frame_data( AVS_VideoFrame *frm, uint8_t *ptr[3], int stride[3], char read, char vertical_flip );
char *ffms_avs_sprintf( const char *str, ... );
char *ffms_avs_sprintf2( char *buf, size_t buf_len, const char *str, ... );

#define init_ErrorInfo(ei_name) \
    char err_msg[1024];\
    FFMS_ErrorInfo ei_name;\
    ei_name.Buffer = err_msg;\
    ei_name.BufferSize = sizeof( err_msg )

#endif
