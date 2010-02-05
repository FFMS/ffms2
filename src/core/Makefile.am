ACLOCAL_AMFLAGS=-I m4

INCLUDES = -I. -I$(top_srcdir)/include -I$(top_srcdir)/src/config @FFMPEG_CFLAGS@ @ZLIB_CPPFLAGS@

lib_LTLIBRARIES = libffms2.la
libffms2_la_LIBADD = @FFMPEG_LIBS@ @ZLIB_LDFLAGS@ -lz @LTUNDEF@
libffms2_la_SOURCES = \
	audiosource.h \
	audiosource.cpp \
	coparser.h \
	ffms.cpp \
	guids.h \
	haaliaudio.cpp \
	haaliindexer.cpp \
	haalivideo.cpp \
	indexing.h \
	indexing.cpp \
	lavfaudio.cpp \
	lavfindexer.cpp \
	lavfvideo.cpp \
	matroskaaudio.cpp \
	matroskaindexer.cpp \
	matroskaparser.h \
	matroskaparser.c \
	matroskavideo.cpp \
	stdiostream.h \
	stdiostream.c \
	utils.h \
	utils.cpp \
	videosource.h \
	videosource.cpp \
	wave64writer.h \
	wave64writer.cpp

include_HEADERS = $(top_srcdir)/include/ffms.h $(top_srcdir)/include/ffmscompat.h
