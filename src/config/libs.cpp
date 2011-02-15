//  Copyright (c) 2009 Karl Blomster
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

#include "msvc-config.h"

extern "C" {
#include <libavutil/avutil.h>
}

// someone claimed that the borland c++ compiler defines _MSC_VER too,
// but nobody cares about borland so let them suffer
#ifdef _MSC_VER


// standard mingw/ffmpeg libraries
#pragma comment(lib, "libgcc.a")
#pragma comment(lib, "libmoldname.a")
#pragma comment(lib, "libmingwex.a")
#pragma comment(lib, "libz.a")
#pragma comment(lib, "libavutil.a")
#pragma comment(lib, "libavcodec.a")
#pragma comment(lib, "libavformat.a")
#pragma comment(lib, "libswscale.a")
#pragma comment(lib, "libwsock32.a")

#if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(50,38,0))
#	pragma comment(lib, "libavcore.a")
#endif


#if defined(WITH_LIBPOSTPROC) && defined(FFMS_USE_POSTPROC)
#pragma comment(lib, "libpostproc.a")
#elif !defined(WITH_LIBPOSTPROC) && defined(FFMS_USE_POSTPROC)
#error You told me to use libpostproc, but I can't find it. Check your library paths or undefine FFMS_USE_POSTPROC.
#endif /*defined(WITH_LIBPOSTPROC) && defined(FFMS_USE_POSTPROC)*/

#ifdef WITH_OPENCORE_AMR_NB
#pragma comment(lib, "libopencore-amrnb.a")
#endif // WITH_OPENCORE_AMR_NB

#ifdef WITH_OPENCORE_AMR_WB
#pragma comment(lib, "libopencore-amrwb.a")
#endif // WITH_OPENCORE_AMR_WB

#ifdef WITH_PTHREAD_GC2
#pragma comment(lib, "libpthreadGC2.a")
#endif // WITH_PTHREAD_GC2


#endif // _MSC_VER
