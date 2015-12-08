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
// but nobody cares about borland so let them suffer.
#ifdef _MSC_VER


#ifdef WITH_GCC_LIBAV
// Standard mingw libraries.
#pragma comment(lib, "libgcc.a")
#pragma comment(lib, "libmoldname.a")
#pragma comment(lib, "libmingwex.a")
#pragma comment(lib, "libz.a")
#pragma comment(lib, "libwsock32.a")
#else
#pragma comment(lib, "zlib.lib")
#endif /* WITH_GCC_LIBAV */

// libav/ffmpeg libs are the same (name) regardless of their compiler.
#pragma comment(lib, "libavutil.a")
#pragma comment(lib, "libavcodec.a")
#pragma comment(lib, "libavformat.a")
#pragma comment(lib, "libswscale.a")
#ifdef WITH_SWRESAMPLE
#pragma comment(lib, "libswresample.a")
#else
#pragma comment(lib, "libavresample.a")
#endif

#ifdef WITH_OPENCORE_AMR_NB
#ifdef WITH_GCC_LIBAV
#pragma comment(lib, "libopencore-amrnb.a")
#else
#pragma comment(lib, "opencore-amrnb.lib")
#endif /* WITH_GCC_LIBAV */
#endif /* WITH_OPENCORE_AMR_NB */

#ifdef WITH_OPENCORE_AMR_WB
#ifdef WITH_GCC_LIBAV
#pragma comment(lib, "libopencore-amrwb.a")
#else
#pragma comment(lib, "opencore-amrwb.lib")
#endif /* WITH_GCC_LIBAV */
#endif /* WITH_OPENCORE_AMR_WB */

#if defined(WITH_PTHREAD_GC2) && defined(WITH_GCC_LIBAV)
#pragma comment(lib, "libpthreadGC2.a")
#elif defined(WITH_PTHREAD_GC2) && !defined(WITH_GCC_LIBAV)
#error pthreads is only supported if libav/ffmpeg was built with gcc.
#endif /* defined(WITH_PTHREAD_GC2) && defined(WITH_GCC_LIBAV) */


#endif /* _MSC_VER */
