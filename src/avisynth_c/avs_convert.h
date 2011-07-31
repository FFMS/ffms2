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

#ifndef FFAVS_CONVERT_H
#define FFAVS_CONVERT_H

#include "avs_common.h"

/* conversion driver mimicking the automatic casting and converting the C++ version has */

static inline char as_bool( AVS_Value val, char def )
{ return avs_is_bool( val ) ? !!avs_as_bool( val ) : !!def; };

static inline AVS_Clip *as_clip( AVS_Value val )
{ return avs_is_clip( val ) ? ffms_avs_lib->avs_take_clip( val, ffms_avs_lib->env ) : NULL; }

static inline AVS_Value as_elt( AVS_Value val, int elt )
{ return elt < avs_array_size( val ) ? avs_array_elt( val, elt ) : avs_void; }

static inline float as_float( AVS_Value val, float def )
{ return avs_is_float( val ) ? avs_as_float( val ) : def; };

static inline int as_int( AVS_Value val, int def )
{ return avs_is_int( val ) ? avs_as_int( val ) : def; }

static inline const char *as_string( AVS_Value val, const char *def )
{ return avs_is_string( val ) ? avs_as_string( val ) : def; };

static inline AVS_Value clip_val( AVS_Clip *clip )
{ AVS_Value v; ffms_avs_lib->avs_set_to_clip( &v, clip ); return v; }

#endif
