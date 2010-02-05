AC_DEFUN([CHECK_ZLIB],[
    AC_ARG_WITH(zlib,AS_HELP_STRING([--with-zlib=DIR],[specify zlib's root installation folder]),
        [ZLIB_HOME="$with_zlib"])

    if test -n "${ZLIB_HOME}" -a "${ZLIB_HOME}" != "yes" -a "${ZLIB_HOME}" != "no" ; then
        ZLIB_CPPFLAGS="-I${ZLIB_HOME}/include"
        ZLIB_LDFLAGS="-L${ZLIB_HOME}/lib"
    fi

    ZLIB_OLD_LDFLAGS=$LDFLAGS
    ZLIB_OLD_CPPFLAGS=$CPPFLAGS
    LDFLAGS="$LDFLAGS $ZLIB_LDFLAGS"
    CPPFLAGS="$CPPFLAGS $ZLIB_CPPFLAGS"
    AC_LANG_PUSH(C)
    AC_CHECK_HEADER(zlib.h, [zlib_cv_zlib_h=yes], [zlib_cv_zlib_h=no])
    AC_CHECK_LIB(z, inflateEnd, [zlib_cv_libz=yes], [zlib_cv_libz=no])
    AC_LANG_POP(C)
    LDFLAGS=$ZLIB_OLD_LDFLAGS
    CPPFLAGS=$ZLIB_OLD_CPPFLAGS
    if test "$zlib_cv_libz" = "yes" -a "$zlib_cv_zlib_h" = "yes"
    then
        HAVE_ZLIB=yes
        AC_SUBST([ZLIB_CPPFLAGS])
        AC_SUBST([ZLIB_LDFLAGS])
    else
        AC_MSG_FAILURE([cannot locate zlib.h and -lz, specify a valid zlib installation using --with-zlib=DIR])
    fi
])
