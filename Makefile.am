ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ffms2.pc

dist_doc_DATA = doc/ffms2-api.html doc/ffms2-changelog.html doc/style.css

INCLUDES = -I. -I$(top_srcdir)/include -I$(top_srcdir)/src/config @FFMPEG_CFLAGS@ @ZLIB_CPPFLAGS@

lib_LTLIBRARIES = src/core/libffms2.la
src_core_libffms2_la_LIBADD = @FFMPEG_LIBS@ @ZLIB_LDFLAGS@ -lz @LTUNDEF@
src_core_libffms2_la_SOURCES = \
	src/core/audiosource.h \
	src/core/audiosource.cpp \
	src/core/coparser.h \
	src/core/ffms.cpp \
	src/core/guids.h \
	src/core/haaliaudio.cpp \
	src/core/haaliindexer.cpp \
	src/core/haalivideo.cpp \
	src/core/indexing.h \
	src/core/indexing.cpp \
	src/core/lavfaudio.cpp \
	src/core/lavfindexer.cpp \
	src/core/lavfvideo.cpp \
	src/core/matroskaaudio.cpp \
	src/core/matroskaindexer.cpp \
	src/core/matroskaparser.h \
	src/core/matroskaparser.c \
	src/core/matroskavideo.cpp \
	src/core/stdiostream.h \
	src/core/stdiostream.c \
	src/core/utils.h \
	src/core/utils.cpp \
	src/core/videosource.h \
	src/core/videosource.cpp \
	src/core/wave64writer.h \
	src/core/wave64writer.cpp

if AVISYNTH
src_core_libffms2_la_SOURCES += \
	src/avisynth_c/avisynth.c \
	src/avisynth_c/avs_lib.c \
	src/avisynth_c/avs_utils.c \
	src/avisynth_c/ff_audsource.c \
	src/avisynth_c/ff_pp.c \
	src/avisynth_c/ff_swscale.c \
	src/avisynth_c/ff_vidsource.c
endif

include_HEADERS = $(top_srcdir)/include/ffms.h $(top_srcdir)/include/ffmscompat.h

bin_PROGRAMS = src/index/ffmsindex
src_index_ffmsindex_SOURCES = src/index/ffmsindex.cpp
src_index_ffmsindex_LDADD = src/core/libffms2.la
