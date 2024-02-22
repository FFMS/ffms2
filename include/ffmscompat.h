//  Copyright (c) 2007-2011 Fredrik Mellbin
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

#ifndef FFMSCOMPAT_H
#define	FFMSCOMPAT_H

#define VERSION_CHECK(LIB, cmp, major, minor, micro) ((LIB) cmp (AV_VERSION_INT(major, minor, micro)))

#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 58, 7, 100)
#define FFMS_TOP_FIELD_FIRST(frame) (!!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST))
#define FFMS_INTERLACED_FRAME(frame) (!!(frame->flags & AV_FRAME_FLAG_INTERLACED))
#define FFMS_IS_KEY_FRAME(frame) (!!(frame->flags & AV_FRAME_FLAG_KEY))
#else
#define FFMS_TOP_FIELD_FIRST(frame) (frame->top_field_first)
#define FFMS_INTERLACED_FRAME(frame) (frame->interlaced_frame)
#define FFMS_IS_KEY_FRAME(frame) (frame->key_frame)
#endif

#endif // FFMSCOMPAT_H
