ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ffms2.pc

dist_doc_DATA = doc/ffms2-api.md doc/ffms2-changelog.md

AM_CPPFLAGS = \
	-I. \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/config \
	-D_FILE_OFFSET_BITS=64 \
	-DFFMS_EXPORTS \
	-D__STDC_CONSTANT_MACROS \
	@LIBAV_CFLAGS@ \
	@ZLIB_CPPFLAGS@ \
	-include config.h
AM_CXXFLAGS = -std=c++11 -fvisibility=hidden

lib_LTLIBRARIES = src/core/libffms2.la
src_core_libffms2_la_LDFLAGS = @src_core_libffms2_la_LDFLAGS@
src_core_libffms2_la_LIBADD = @LIBAV_LIBS@ @AVRESAMPLE_LIBS@ @ZLIB_LDFLAGS@ -lz @LTUNDEF@
src_core_libffms2_la_SOURCES = \
	src/core/audiosource.cpp \
	src/core/audiosource.h \
	src/core/ffms.cpp \
	src/core/filehandle.cpp \
	src/core/filehandle.h \
	src/core/indexing.cpp \
	src/core/indexing.h \
	src/core/lavfaudio.cpp \
	src/core/lavfindexer.cpp \
	src/core/lavfvideo.cpp \
	src/core/track.cpp \
	src/core/track.h \
	src/core/utils.cpp \
	src/core/utils.h \
	src/core/videosource.cpp \
	src/core/videosource.h \
	src/core/videoutils.cpp \
	src/core/videoutils.h \
	src/core/wave64writer.cpp \
	src/core/wave64writer.h \
	src/core/zipfile.cpp \
	src/core/zipfile.h \
	src/vapoursynth/VapourSynth.h \
	src/vapoursynth/vapoursource.cpp \
	src/vapoursynth/vapoursource.h \
	src/vapoursynth/vapoursynth.cpp

include_HEADERS = $(top_srcdir)/include/ffms.h $(top_srcdir)/include/ffmscompat.h

bin_PROGRAMS = src/index/ffmsindex
src_index_ffmsindex_SOURCES = src/index/ffmsindex.cpp
src_index_ffmsindex_LDADD = src/core/libffms2.la
