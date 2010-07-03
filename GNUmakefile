# Makefile

include config.mak

all: default

CORE_C   = src/core/matroskaparser.c src/core/stdiostream.c

CORE_CXX = src/core/audiosource.cpp src/core/ffms.cpp src/core/haaliaudio.cpp src/core/haaliindexer.cpp \
           src/core/haalivideo.cpp src/core/indexing.cpp src/core/lavfaudio.cpp src/core/lavfindexer.cpp \
           src/core/lavfvideo.cpp src/core/matroskaaudio.cpp src/core/matroskaindexer.cpp src/core/matroskavideo.cpp \
           src/core/utils.cpp src/core/videosource.cpp src/core/wave64writer.cpp 

IDX_CXX = src/index/ffmsindex.cpp

SO_C =

# Optional module sources
ifeq ($(AVISYNTH), yes)
SO_C += src/avisynth_c/avisynth.c src/avisynth_c/avs_lib.c src/avisynth_c/avs_utils.c src/avisynth_c/ff_audsource.c \
        src/avisynth_c/ff_pp.c src/avisynth_c/ff_swscale.c src/avisynth_c/ff_vidsource.c
endif

CORE_O = $(CORE_C:%.c=%.o) $(CORE_CXX:%.cpp=%.o)
IDX_O = $(IDX_CXX:%.cpp=%.o)
SO_O = $(SO_C:%.c=%.o)

default: $(DEP) ffmsindex$(EXE)

libffms$(API).a: .depend $(CORE_O)
	$(AR) rc libffms$(API).a $(CORE_O)
	$(RANLIB) libffms$(API).a

$(SONAME): .depend $(CORE_O) $(SO_O)
	$(CXX) -shared -o $@ $(CORE_O) $(SO_O) $(SOFLAGS) $(SOFLAGS_USER) $(LDFLAGS)

ffmsindex$(EXE): $(CORE_O) $(IDX_O) libffms$(API).a
	$(CXX) -o $@ $+ $(LDFLAGS)

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

clean:
	rm -f $(CORE_O) $(SO_O) $(IDX_O) $(SONAME) *.a ffmsindex ffmsindex$(EXE) .depend TAGS
distclean: clean
	rm -f config.mak config.h config.log ffms$(API).pc
