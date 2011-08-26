# Makefile

include config.mak

all: default

CORE_C   = src/core/matroskaparser.c src/core/stdiostream.c

CORE_CXX = src/core/audiosource.cpp src/core/ffms.cpp src/core/haaliaudio.cpp src/core/haaliindexer.cpp \
           src/core/haalivideo.cpp src/core/indexing.cpp src/core/lavfaudio.cpp src/core/lavfindexer.cpp \
           src/core/lavfvideo.cpp src/core/matroskaaudio.cpp src/core/matroskaindexer.cpp src/core/matroskavideo.cpp \
           src/core/utils.cpp src/core/videosource.cpp src/core/wave64writer.cpp src/core/numthreads.cpp \
           src/core/videoutils.cpp

IDX_CXX = src/index/ffmsindex.cpp

SO_C =

# Optional module sources
ifeq ($(AVISYNTH), yes)
SO_C += src/avisynth_c/avisynth.c src/avisynth_c/avs_lib.c src/avisynth_c/avs_utils.c src/avisynth_c/ff_audsource.c \
        src/avisynth_c/ff_swscale.c src/avisynth_c/ff_vidsource.c

ifeq ($(FFMS_USE_POSTPROC), yes)
SO_C += src/avisynth_c/ff_pp.c
endif
endif

CORE_O = $(CORE_C:%.c=%.o) $(CORE_CXX:%.cpp=%.o)
IDX_O = $(IDX_CXX:%.cpp=%.o)
SO_O = $(SO_C:%.c=%.o)

INDEX_LINK=libffms.a
ifneq ($(SONAME),)
INDEX_LINK=$(SONAME)
endif
ifneq ($(IMPLIBNAME),)
INDEX_LINK=$(IMPLIBNAME)
endif

default: $(DEP) ffmsindex$(EXE)

libffms.a: .depend $(CORE_O)
	$(AR) rc libffms.a $(CORE_O)
	$(RANLIB) libffms.a

$(SONAME): .depend $(CORE_O) $(SO_O)
	$(CXX) -shared -o $@ $(CORE_O) $(SO_O) $(SOFLAGS) $(SOFLAGS_USER) $(LDFLAGS)

ffmsindex$(EXE): $(IDX_O) libffms.a $(SONAME)
	$(CXX) -o $@ $(IDX_O) $(INDEX_LINK) $(LDFLAGS)

.depend: config.mak
	@rm -f .depend
	@$(foreach SRC, $(CORE_C), $(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -MT $(SRC:%.c=%.o) -MM -g0 1>> .depend;)
	@$(foreach SRC, $(CORE_CXX) $(IDX_CXX), $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC) -MT $(SRC:%.cpp=%.o) -MM -g0 1>> .depend;)

config.mak:
	./configure

depend: .depend
ifneq ($(wildcard .depend),)
include .depend
endif

install: ffmsindex$(EXE) $(SONAME)
	install -d $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(includedir)
	install -d $(DESTDIR)$(libdir)
	install -d $(DESTDIR)$(libdir)/pkgconfig
	install -m 644 include/ffms.h $(DESTDIR)$(includedir)
	install -m 644 libffms.a $(DESTDIR)$(libdir)
	install -m 644 ffms.pc $(DESTDIR)$(libdir)/pkgconfig
	install ffmsindex$(EXE) $(DESTDIR)$(bindir)
	$(RANLIB) $(DESTDIR)$(libdir)/libffms.a
ifeq ($(SYS),MINGW)
	$(if $(SONAME), install -m 755 $(SONAME) $(DESTDIR)$(bindir))
else
	$(if $(SONAME), ln -f -s $(SONAME) $(DESTDIR)$(libdir)/libffms.$(SOSUFFIX))
	$(if $(SONAME), install -m 755 $(SONAME) $(DESTDIR)$(libdir))
endif
	$(if $(IMPLIBNAME), install -m 644 $(IMPLIBNAME) $(DESTDIR)$(libdir))

uninstall:
	rm -f $(DESTDIR)$(includedir)/ffms.h $(DESTDIR)$(libdir)/libffms.a
	rm -f $(DESTDIR)$(bindir)/ffmsindex$(EXE) $(DESTDIR)$(libdir)/pkgconfig/ffms.pc
	$(if $(SONAME), rm -f $(DESTDIR)$(libdir)/$(SONAME) $(DESTDIR)$(libdir)/libffms.$(SOSUFFIX) $(DESTDIR)$(bindir)/$(SONAME))
	$(if $(IMPLIBNAME), rm -f $(DESTDIR)$(libdir)/$(IMPLIBNAME))

clean:
	rm -f $(CORE_O) $(SO_O) $(IDX_O) $(SONAME) *.a ffmsindex ffmsindex$(EXE) .depend TAGS
distclean: clean
	rm -f config.mak config.h config.log ffms.pc
