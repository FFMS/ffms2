AC_PREREQ([2.58])
AC_INIT([ffms2],[esyscmd([sh version.sh])])
AC_CONFIG_SRCDIR([src/core/ffms.cpp])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 subdir-objects])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([disable])

AC_MSG_CHECKING([if debug build is enabled])

AC_ARG_ENABLE([debug],
        [AC_HELP_STRING([--enable-debug],
            [Enable debug build. [default=no]])],
        [enable_debug=yes],
        [enable_debug=no]
        )

AC_MSG_RESULT([$enable_debug])

if test "$enable_debug" = yes; then
    OPT_FLAGS="-O0 -g"
else
    OPT_FLAGS="-O3"
fi

if test -z "$CFLAGS"; then
    CFLAGS="$OPT_FLAGS"
fi

if test -z "$CXXFLAGS"; then
    CXXFLAGS="$OPT_FLAGS"
fi

AC_CONFIG_HEADERS([src/config/config.h])
AC_PROG_CC
AC_PROG_CXX

AC_CANONICAL_HOST
AS_CASE([$host],
        [*mingw* | *cygwin*], [
            AC_ENABLE_STATIC
            AC_DISABLE_SHARED
         ], [
            AC_ENABLE_SHARED
            AC_DISABLE_STATIC])

AC_PROG_LIBTOOL

if echo "$host" | $GREP "cygwin" >/dev/null 2>&1 && test "$enable_shared" = "yes"; then
    AC_MSG_ERROR([Shared build is broken on cygwin.
                  Please remove --enable-shared from your configure options or build with another --host.])
fi

AC_FUNC_ALLOCA
if ! test "x$ac_cv_working_alloca_h" = "xyes" && ! test "x$ac_cv_func_alloca_works" = "xyes"; then
    AC_MSG_FAILURE([ffms2 requires alloca to compile])
fi

dnl Workaround for a bug in libtool
dnl The windows libtool uses a file magic checking method that only accepts
dnl dynamic libraries. Change it for libtool's alternative checking method.
if test "$lt_cv_file_magic_cmd" = "func_win32_libid" ; then
    deplibs_check_method='file_magic file format pei*-(i386|x86-64)|(.*architecture: i386)?'
    file_magic_cmd='$OBJDUMP -f'
fi

FFMS_VERSION="$(sh $(dirname -- "$0")/version.sh)"
AC_SUBST([FFMS_VERSION])

_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS -fvisibility=hidden"

AC_LANG_PUSH([C++])

AC_MSG_CHECKING([if -fvisibility=hidden works])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([char foo;])],
    [fvisibility_works=yes],
    [fvisibility_works=no]
    )

AC_MSG_RESULT([$fvisibility_works])

if test "$fvisibility_works" = no; then
    CXXFLAGS="$_CXXFLAGS"
fi

AC_LANG_POP([C++])

CHECK_ZLIB

AC_MSG_CHECKING([for CoUninitialize in -lole32])
_LIBS="$LIBS"
LIBS="-lole32 $LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
               #include <objbase.h>
               ]],[[
                   CoUninitialize();
                   return 0;
               ]])], [AC_MSG_RESULT([yes])], [LIBS="$_LIBS"; AC_MSG_RESULT([no])])


dnl Save CFLAGS and LIBS for later, as anything else we add will be from pkg-config
dnl and thus should be separate in our .pc file.
_CFLAGS="$CFLAGS"
_LIBS="$LIBS"

PKG_PROG_PKG_CONFIG([0.22])
pkgconfigdir="\$(libdir)/pkgconfig"
AC_SUBST(pkgconfigdir)

PKG_CHECK_MODULES(LIBAV, [libavformat >= 53.20.0 libavcodec >= 53.24.0 libswscale >= 0.7.0 libavutil >= 51.21.0 ])
CPPFLAGS="$CPPFLAGS -D__STDC_CONSTANT_MACROS"
AC_SUBST([LIBAV_CFLAGS])
AC_SUBST([LIBAV_LIBS])

AC_DEFUN([TEST_IS_FFMPEG],
         [AC_LINK_IFELSE([AC_LANG_PROGRAM([[
            #include <libavcodec/avcodec.h>
            #include <libswscale/swscale.h>
            ]],[[
                #if LIBAVCODEC_VERSION_MICRO >= 100
                /* is ffmpeg */
                #elif LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 47, 100)
                /* hopefully libav doesn''t add G2M before 53.47.... */
                (void)CODEC_ID_G2M;
                #else
                #error is libav or old enough that the distinction doesn't matter
                #endif
            ]])], [eval $1=yes], [eval $1=no])
        ])

AC_MSG_CHECKING([whether linking with FFmpeg or Libav])
CFLAGS="$_CFLAGS $LIBAV_CFLAGS"
TEST_IS_FFMPEG([FFMS_USE_FFMPEG_COMPAT])

if test "$FFMS_USE_FFMPEG_COMPAT" = yes; then
   AC_DEFINE([FFMS_USE_FFMPEG_COMPAT], [1], [Building with FFmpeg?])
   _lav_name=FFmpeg
else
   _lav_name=Libav
fi
AC_MSG_RESULT([$_lav_name])

AC_DEFUN([TEST_LIBAV],
         [AC_LINK_IFELSE([AC_LANG_PROGRAM([[
            #include <libavcodec/avcodec.h>
            #include <libswscale/swscale.h>
            ]],[[
                avcodec_register_all();
                swscale_version();
                #ifdef FFMS_USE_FFMPEG_COMPAT
                int bogus = CODEC_ID_G2M;
                (void) bogus;
                #endif
            ]])], [eval $1=yes], [eval $1=no])
        ])

AC_MSG_CHECKING([whether $_lav_name works])
LIBS="$_LIBS $LIBAV_LIBS"
TEST_LIBAV([LIBAV_WORKS])
AC_MSG_RESULT([$LIBAV_WORKS])
if test "$LIBAV_WORKS" = no; then
AC_MSG_FAILURE([cannot link with $_lav_name])
fi

AC_MSG_CHECKING([whether avformat_get_riff_video_tags is available])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
              #include <libavformat/avformat.h>
            ]],
            [[
              #if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(54, 1, 0)
              const struct AVCodecTag *tags = avformat_get_riff_video_tags();
              (void)tags;
              #endif
            ]])],
            [
              AC_DEFINE([RETARDED_LIBAV_RELEASE_BRANCH_WHICH_INTRODUCED_AVFORMAT_GET_RIFF_VIDEO_TAGS_AT_A_VERSION_NUMBER_WHICH_HAD_ALREADY_BEEN_USED], [1], [ ])
              AC_MSG_RESULT([yes])
            ],
            [
              AC_MSG_RESULT([no])
            ])

AC_ARG_ENABLE(avresample,
              AS_HELP_STRING([--enable-avresample],
                             [use libavresample for audio resampling]))
AS_IF([test x$enable_avresample != xno], [
  PKG_CHECK_MODULES(AVRESAMPLE, [libavresample >= 1.0.0], [enable_avresample=yes], [
    AS_IF([test x$enable_avresample = xyes],
          [AC_MSG_ERROR([--enable-avresample was specified, but avresample 1.0.0+ could not be found.])])
    enable_avresample=no
  ])
])

AS_IF([test x$enable_avresample != xno],
      [libavresample="libavresample"
       AC_DEFINE([WITH_AVRESAMPLE], [1], [Use avresample])])

AC_SUBST([AVRESAMPLE_CFLAGS])
AC_SUBST([AVRESAMPLE_LIBS])
AC_SUBST([libavresample])

AC_MSG_CHECKING([whether -Wl,-Bsymbolic is needed])
if test "$enable_shared" = yes; then
    _LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS -shared $lt_prog_compiler_pic"
    TEST_LIBAV([no_bsymbolic])
    if test "$no_bsymbolic" = "no"; then
        LDFLAGS="$LDFLAGS -Wl,-Bsymbolic"
        TEST_LIBAV([bsymbolic])
        if test "$bsymbolic" = "yes"; then
            LDFLAGS="$_LDFLAGS -Wl,-Bsymbolic"
        else
            AC_MSG_RESULT($bsymbolic)
            AC_MSG_FAILURE([cannot build ffms2 as a shared library])
        fi
    else
        bsymbolic=no
        LDFLAGS="$_LDFLAGS"
    fi
    LDFLAGS="$LDFLAGS -version-info 2"
else
    bsymbolic=no
fi
CFLAGS="$_CFLAGS -std=c99"
CPPFLAGS="$CPPFLAGS -D_FILE_OFFSET_BITS=64 -DFFMS_EXPORTS"
LIBS="$_LIBS"
AC_MSG_RESULT($bsymbolic)

if echo "$host" | $GREP "mingw" >/dev/null 2>&1; then
    LTUNDEF="-no-undefined"
fi
AC_SUBST([LTUNDEF])

AC_CONFIG_FILES([
Makefile
ffms2.pc
])
AC_OUTPUT

