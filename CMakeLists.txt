CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

INCLUDE (CheckIncludeFile)

PROJECT(FFmpegSource2)

SET (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

IF (WIN32)
	ADD_DEFINITIONS(-DFFMS_EXPORTS -g)
    
    CHECK_INCLUDE_FILE(atlbase.h HAVE_ATL)
    IF (HAVE_ATL)
        ADD_DEFINITIONS(-DHAALISOURCE)
    ENDIF (HAVE_ATL)
    
    SET (EXTRA_LIBS ws2_32 avifil32)
    
    IF (MSVC)
        SET (AVS_SOURCES ffavisynth.cpp ffavssource.cpp ffpp.cpp ffswscale.cpp)
    ENDIF (MSVC)

ENDIF (WIN32)

FIND_PACKAGE(FFmpeg REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)
FIND_PACKAGE(BZip2 REQUIRED)
FIND_PACKAGE(Threads)

IF(Threads_FOUND AND CMAKE_HAVE_PTHREAD_H)
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${CMAKE_THREAD_LIBS_INIT})
ENDIF(Threads_FOUND AND CMAKE_HAVE_PTHREAD_H)

SET(FFMS2_LIBS ${FFMPEG_LIBRARIES} ${FFMPEG_SWS_LIBRARIES}
            ${FFMPEG_PP_LIBRARIES} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARIES}
            ${EXTRA_LIBS})
            
SET(FFMS2_SRCS matroskaparser.c stdiostream.c ffaudiosource.cpp
               ffhaaliaudio.cpp fflavfaudio.cpp ffmatroskaaudio.cpp
               ffvideosource.cpp ffhaalivideo.cpp fflavfvideo.cpp
               ffmatroskavideo.cpp ffms.cpp wave64writer.cpp utils.cpp
               indexing.cpp ffhaaliindexer.cpp fflavfindexer.cpp
               ffmatroskaindexer.cpp)


ADD_LIBRARY(FFMS2Core STATIC ${FFMS2_SRCS})
TARGET_LINK_LIBRARIES(FFMS2Core ${FFMS2_LIBS})

SET(FFMS2_LIB FFMS2Core)

IF(AVS_SOURCES)
    ADD_LIBRARY(FFMS2 SHARED ${AVS_SOURCES})
    TARGET_LINK_LIBRARIES(FFMS2 FFMS2Core ${FFMS2_LIBS})
    ADD_DEPENDENCIES(FFMS2 FFMS2Core)
    SET(FFMS2_LIB FFMS2)
ENDIF(AVS_SOURCES)

ADD_EXECUTABLE(ffmsindex ffmsindex.cpp)
TARGET_LINK_LIBRARIES(ffmsindex ${FFMS2_LIB} ${FFMS2_LIBS})

ADD_EXECUTABLE(ffmstest ffmstest.cpp)
TARGET_LINK_LIBRARIES(ffmstest ${FFMS2_LIB} ${FFMS2_LIBS})

ADD_DEPENDENCIES(ffmstest ${FFMS2_LIB})
ADD_DEPENDENCIES(ffmsindex ${FFMS2_LIB})
