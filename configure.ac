AC_PREREQ([2.58])
AC_INIT([ffms2],[esyscmd([sh version.sh])])
AC_CONFIG_SRCDIR([src/core/ffms.cpp])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 subdir-objects])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([disable])

VERSION_INFO="5:0:0"

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
    CFLAGS="$OPT_FLAGS -Wall -Wextra"
fi

if test -z "$CXXFLAGS"; then
    CXXFLAGS="$OPT_FLAGS -Wall -Wextra"
fi

AC_ARG_ENABLE([avisynth],
        [AC_HELP_STRING([--enable-avisynth],
            [Enable AviSynth+ plugin. [default=no]])],
        [enable_avisynth=yes],
        [enable_avisynth=no]
        )

AC_MSG_RESULT([$enable_avisynth])
AM_CONDITIONAL([AVISYNTH], [test "x$enable_avisynth" != "xno"])

AC_CONFIG_HEADERS([src/config/config.h])
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LN_S
AC_PROG_MKDIR_P

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

dnl Workaround for a bug in libtool
dnl The windows libtool uses a file magic checking method that only accepts
dnl dynamic libraries. Change it for libtool's alternative checking method.
if test "$lt_cv_file_magic_cmd" = "func_win32_libid" ; then
    deplibs_check_method='file_magic file format pei*-(i386|x86-64)|(.*architecture: i386)?'
    file_magic_cmd='$OBJDUMP -f'
fi

FFMS_VERSION="$(sh $(dirname -- "$0")/version.sh)"
AC_SUBST([FFMS_VERSION])

CHECK_ZLIB

dnl Save CFLAGS and LIBS for later, as anything else we add will be from pkg-config
dnl and thus should be separate in our .pc file.
_CFLAGS="$CFLAGS"
_CPPFLAGS="$CPPFLAGS"
_LIBS="$LIBS"

PKG_PROG_PKG_CONFIG([0.22])
pkgconfigdir="\$(libdir)/pkgconfig"
AC_SUBST(pkgconfigdir)

PKG_CHECK_MODULES(FFMPEG, [libavformat >= 60.16.0 libavcodec >= 60.31.0 libswscale >= 7.5.0 libavutil >= 58.29.0 libswresample >= 4.12.0])

dnl As of 0eec06ed8747923faa6a98e474f224d922dc487d ffmpeg only adds -lrt to lavc's
dnl LIBS, but lavu needs it, so move it to the end if it's present
FFMPEG_LIBS=$(echo $FFMPEG_LIBS | sed 's/\(.*\)-lrt \(.*\)/\1\2 -lrt/')

AC_SUBST([FFMPEG_CFLAGS])
AC_SUBST([FFMPEG_LIBS])

CPPFLAGS="$CPPFLAGS -D__STDC_CONSTANT_MACROS"
CFLAGS="$_CFLAGS $FFMPEG_CFLAGS"

AC_DEFUN([TEST_FFMPEG],
         [AC_LINK_IFELSE([AC_LANG_PROGRAM([[
            #include <libavformat/avformat.h>
            #include <libswscale/swscale.h>
            ]],[[
                avformat_network_init();
                swscale_version();
            ]])], [eval $1=yes], [eval $1=no])
        ])

AC_MSG_CHECKING([whether FFmpeg works])
LIBS="$_LIBS $FFMPEG_LIBS"
TEST_FFMPEG([FFMPEG_WORKS])
AC_MSG_RESULT([$FFMPEG_WORKS])
if test "$FFMPEG_WORKS" = no; then
AC_MSG_FAILURE([cannot link with FFmpeg])
fi

src_core_libffms2_la_LDFLAGS=""
AC_MSG_CHECKING([whether -Wl,-Bsymbolic is needed])
if test "$enable_shared" = yes; then
    _LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS -shared $lt_prog_compiler_pic"
    TEST_FFMPEG([no_bsymbolic])
    if test "$no_bsymbolic" = "no"; then
        LDFLAGS="$LDFLAGS -Wl,-Bsymbolic"
        TEST_FFMPEG([bsymbolic])
        if test "$bsymbolic" = "yes"; then
            src_core_libffms2_la_LDFLAGS="$src_core_libffms2_la_LDFLAGS -Wl,-Bsymbolic"
        else
            AC_MSG_RESULT($bsymbolic)
            AC_MSG_FAILURE([cannot build ffms2 as a shared library])
        fi
    else
        bsymbolic=no
    fi
    LDFLAGS="$_LDFLAGS"
    src_core_libffms2_la_LDFLAGS="$src_core_libffms2_la_LDFLAGS -version-info $VERSION_INFO"
else
    bsymbolic=no
fi
AC_SUBST([src_core_libffms2_la_LDFLAGS])

CFLAGS="$_CFLAGS"
CPPFLAGS="$_CPPFLAGS"
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

