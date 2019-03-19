# Where to find user code.
USER_DIR = ..

# Points to the root of Google Test, relative to where this file is.
# Remember to tweak this if you move this file.
GTEST_DIR = $(USER_DIR)/test/googletest/googletest

# Where to find sample files
SAMPLES_DIR =

# URL to sync samples from
SAMPLES_URL =

# Flags passed to the preprocessor.
# Set Google Test's header directory as a system directory, such that
# the compiler doesn't generate warnings in Google Test headers.
CPPFLAGS += -isystem $(GTEST_DIR)/include \
	-I$(USER_DIR)/include \
	-D_FILE_OFFSET_BITS=64 \
	-DFFMS_EXPORTS \
	-D__STDC_CONSTANT_MACROS \
	-DSAMPLES_DIR=$(SAMPLES_DIR)

# Flags passed to the C++ compiler.
CXXFLAGS += -g -Wall -Wextra -pthread -std=c++11 -fvisibility=hidden

# All tests produced by this Makefile. Remember to add new tests you
# created to the list.
TESTS = indexer        \
        hdr            \
        display_matrix

RUNTESTS = $(subst $() $(),-run$() $(),$(TESTS))-run

# All the sample files we need to sync
SAMPLES = test.mp4                \
          hdr10tags-both.mkv      \
          hdr10tags-container.mkv \
          hdr10tags-stream.mp4    \
          qrvideo_hflip_90.mov    \
          qrvideo_hflip_270.mov   \
          qrvideo_vflip.mov

# All Google Test headers. Usually you shouldn't change this
# definition.
GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h $(GTEST_DIR)/include/gtest/internal/*.h

# House-keeping build targets.

all: $(TESTS)

run: all $(RUNTESTS)

sync:
	@for i in $(SAMPLES); do \
		if [ ! -f "$(SAMPLES_DIR)/$$i" ]; then \
			wget -O $(SAMPLES_DIR)/$$i $(SAMPLES_URL)/$$i; \
		fi \
	done

clean:
	rm -f $(TESTS) gtest.a gtest_main.a *.o
	rm -rf .libs

# Builds gtest.a and gtest_main.a.

# Usually you shouldn't tweak such internal variables, indicated by a
# trailing _.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized. This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
gtest-all.o: $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest-all.cc

gtest_main.o: $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest_main.cc

gtest.a: gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

gtest_main.a: gtest-all.o gtest_main.o
	$(AR) $(ARFLAGS) $@ $^

tests.o: $(USER_DIR)/test/tests.cpp $(USER_DIR)/include/ffms.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/test/tests.cpp

hdr.o: $(USER_DIR)/test/hdr.cpp $(USER_DIR)/include/ffms.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/test/hdr.cpp

display_matrix.o: $(USER_DIR)/test/display_matrix.cpp $(USER_DIR)/include/ffms.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/test/display_matrix.cpp

indexer.o: $(USER_DIR)/test/indexer.cpp $(USER_DIR)/include/ffms.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(USER_DIR)/test/indexer.cpp

indexer: indexer.o tests.o gtest_main.a ../src/core/libffms2.la
	../libtool --tag=CXX --mode=link $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o indexer indexer.o tests.o gtest_main.a -lavutil ../src/core/libffms2.la

indexer-run:
	@./indexer

hdr: hdr.o gtest_main.a ../src/core/libffms2.la
	../libtool --tag=CXX --mode=link $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o hdr hdr.o gtest_main.a -lavutil ../src/core/libffms2.la

hdr-run:
	@./hdr

display_matrix: display_matrix.o gtest_main.a ../src/core/libffms2.la
	../libtool --tag=CXX --mode=link $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o display_matrix display_matrix.o gtest_main.a -lavutil ../src/core/libffms2.la

display_matrix-run:
	@./display_matrix
