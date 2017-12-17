# Makefile

include config.mak

all: default

ifndef V
$(foreach VAR,CC CXX AR RANLIB RC,\
    $(eval override $(VAR) = @printf " %s\t%s\n" $(VAR) "$$@"; $($(VAR))))
endif

CORE_C   =

CORE_CXX = src/core/audiosource.cpp     \
           src/core/ffms.cpp            \
           src/core/filehandle.cpp      \
           src/core/indexing.cpp        \
           src/core/track.cpp           \
           src/core/utils.cpp           \
           src/core/videosource.cpp     \
           src/core/videoutils.cpp      \
           src/core/zipfile.cpp

IDX_CXX = src/index/ffmsindex.cpp

SO_C =

SO_CXX =

# Optional module sources
ifeq ($(AVISYNTH), yes)
SO_C += src/avisynth_c/avisynth.c     \
        src/avisynth_c/avs_lib.c      \
        src/avisynth_c/avs_utils.c    \
        src/avisynth_c/ff_audsource.c \
        src/avisynth_c/ff_vidsource.c
endif

ifeq ($(AVXSYNTH), yes)
SO_CXX += src/avxsynth/avisynth.cpp   \
          src/avxsynth/avssources.cpp \
          src/avxsynth/avsutils.cpp   \
          src/avxsynth/ffswscale.cpp
endif

ifeq ($(VAPOURSYNTH),yes)
SO_CXX += src/vapoursynth/vapoursource.cpp \
          src/vapoursynth/vapoursynth.cpp
endif

CORE_O = $(CORE_C:%.c=%.o) $(CORE_CXX:%.cpp=%.o)
IDX_O = $(IDX_CXX:%.cpp=%.o)
SO_O = $(SO_C:%.c=%.o) $(SO_CXX:%.cpp=%.o)

ifeq ($(SYS), MINGW)
IDX_O += ffmsindexexe.o
IDX_LDFLAGS += -municode
ifneq ($(SONAME),)
SO_O += ffmsdll.o
endif
endif

INDEX_LINK=libffms2.a
ifneq ($(SONAME),)
INDEX_LINK=$(SONAME)
endif
ifneq ($(IMPLIBNAME),)
INDEX_LINK=$(IMPLIBNAME)
endif

ffmsdll.o: ffmsdll.rc.h
ffmsindexexe.o: ffmsindexexe.rc.h
%.o: %.rc
	$(RC) --include-dir=. -o $@ $<

default: $(DEP) ffmsindex$(EXE)

libffms2.a: .depend $(CORE_O)
	$(AR) rc libffms2.a $(CORE_O)
	$(RANLIB) libffms2.a

$(SONAME): .depend $(CORE_O) $(SO_O) $(SO_CXX)
	$(CXX) -shared -o $@ $(CORE_O) $(SO_O) $(SOFLAGS) $(SOFLAGS_USER) $(LDFLAGS)

ffmsindex$(EXE): $(IDX_O) libffms2.a $(SONAME)
	$(CXX) -o $@ $(IDX_O) $(INDEX_LINK) $(LDFLAGS) $(IDX_LDFLAGS)

define \n


endef

.depend: config.mak
	@rm -f .depend
	@$(foreach SRC_C, $(CORE_C), \
            $(PLC) $(CPPFLAGS) $(CFLAGS) $(SRC_C) -MT $(SRC_C:%.c=%.o) -MM -g0 1>> .depend;${\n})
	@$(foreach SRC_CXX, $(CORE_CXX) $(IDX_CXX), \
            $(PLPL) $(CPPFLAGS) $(CXXFLAGS) $(SRC_CXX) -MT $(SRC_CXX:%.cpp=%.o) -MM -g0 1>> .depend;${\n})

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
	install -m 644 libffms2.a $(DESTDIR)$(libdir)
	install -m 644 ffms2.pc $(DESTDIR)$(libdir)/pkgconfig
	install ffmsindex$(EXE) $(DESTDIR)$(bindir)
	$(RANLIBX) $(DESTDIR)$(libdir)/libffms2.a
ifeq ($(SYS),MINGW)
	$(if $(SONAME), install -m 755 $(SONAME) $(DESTDIR)$(bindir))
else
	$(if $(SONAME), ln -f -s $(SONAME) $(DESTDIR)$(libdir)/libffms2.$(SOSUFFIX))
	$(if $(SONAME), install -m 755 $(SONAME) $(DESTDIR)$(libdir))
ifeq ($(AVXSYNTH), yes)
	install -d $(DESTDIR)$(avxplugindir)
	install -m 644 etc/FFMS2.avsi $(DESTDIR)$(avxplugindir)/FFMS2.avsi
	$(if $(SONAME), ln -f -s $(libdir)/$(SONAME) $(DESTDIR)$(avxplugindir)/libavxffms2.so)
endif
ifeq ($(VAPOURSYNTH), yes)
	install -d $(DESTDIR)$(vsplugindir)
	$(if $(SONAME), ln -f -s $(libdir)/$(SONAME) $(DESTDIR)$(vsplugindir)/libffms2.$(SOSUFFIX))
endif
endif
	$(if $(IMPLIBNAME), install -m 644 $(IMPLIBNAME) $(DESTDIR)$(libdir))

install-avs:
	install -d $(DESTDIR)$(bindir)
	install ffmsindex$(EXE) $(DESTDIR)$(bindir)
ifeq ($(SYS),MINGW)
	cp etc/* $(DESTDIR)$(bindir)
	mv $(DESTDIR)$(bindir)/FFMS2-cplugin.avsi $(DESTDIR)$(bindir)/FFMS2.avsi
	cp -R doc $(DESTDIR)$(bindir)
	$(if $(SONAME), install -m 755 $(SONAME) $(DESTDIR)$(bindir))
endif

uninstall:
	rm -f $(DESTDIR)$(includedir)/ffms.h $(DESTDIR)$(libdir)/libffms2.a
	rm -f $(DESTDIR)$(bindir)/ffmsindex$(EXE) $(DESTDIR)$(libdir)/pkgconfig/ffms2.pc
	$(if $(SONAME), rm -f $(DESTDIR)$(libdir)/$(SONAME) $(DESTDIR)$(libdir)/libffms2.$(SOSUFFIX) $(DESTDIR)$(bindir)/$(SONAME))
	$(if $(IMPLIBNAME), rm -f $(DESTDIR)$(libdir)/$(IMPLIBNAME))

clean:
	rm -f $(CORE_O) $(SO_O) $(IDX_O) $(SONAME) *.a ffmsindex ffmsindex$(EXE) .depend TAGS
distclean: clean
	rm -f config.mak config.h config.log ffms2.pc
