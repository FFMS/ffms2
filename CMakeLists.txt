# FFMS2 CMake list file
#
# If this project gets bigger you can start making different files in different
# folders.

CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

INCLUDE (CheckIncludeFile)

# Adding some folders to include.
# I bet there's a more beautiful way to do this, seriously.
# But this "works" atm.
#  --JEEB
INCLUDE_DIRECTORIES("include")

PROJECT(FFmpegSource2)

SET (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/build-cmake")

ADD_DEFINITIONS(-DFFMS_EXPORTS)

# If the script cannot find ffmpeg, update your tree from svn and check if
# FindFFMPEG.cmake's name has been changed to FindFFmpeg.cmake, which is what
# is needed.
FIND_PACKAGE(FFmpeg REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)
# Old Checks for static linking.
#FIND_PACKAGE(BZip2)
FIND_PACKAGE(Threads)
FIND_PACKAGE(Faad)

IF(Threads_FOUND AND CMAKE_HAVE_PTHREAD_H)
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${CMAKE_THREAD_LIBS_INIT})
ENDIF(Threads_FOUND AND CMAKE_HAVE_PTHREAD_H)

#message ("Faad_LIBRARIES = ${Faad_LIBRARIES}")

#if (CMAKE_USE_PTHREADS_INIT)
#    set( FFMS2_LIBS CMAKE_THREAD_LIBS_INIT )
#        ADD_DEFINITIONS( CMAKE_THREAD_LIBS_INIT )
#endif ( CMAKE_USE_PTHREADS_INIT )

SET(FFMS2_LIBS ${FFMPEG_LIBRARIES} ${FFMPEG_SWS_LIBRARIES}
            ${FFMPEG_PP_LIBRARIES} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARIES}
            ${EXTRA_LIBS} ${Faad_LIBRARIES} ${Threads_LIBRARIES} )
            
            #${Faad_LIBRARIES}
            
SET(FFMS2_SRCS src/core/matroskaparser.c src/core/stdiostream.c
               src/core/ffaudiosource.cpp src/core/ffhaaliaudio.cpp
               src/core/fflavfaudio.cpp src/core/ffmatroskaaudio.cpp
               src/core/ffvideosource.cpp src/core/ffhaalivideo.cpp
               src/core/fflavfvideo.cpp src/core/ffmatroskavideo.cpp
               src/core/ffms.cpp src/core/wave64writer.cpp src/core/utils.cpp
               src/core/indexing.cpp src/core/ffhaaliindexer.cpp
               src/core/fflavfindexer.cpp src/core/ffmatroskaindexer.cpp)


ADD_LIBRARY(FFMS2Core SHARED ${FFMS2_SRCS})
TARGET_LINK_LIBRARIES(FFMS2Core ${FFMS2_LIBS})

SET(FFMS2_LIB FFMS2Core)

IF(AVS_SOURCES)
    ADD_LIBRARY(FFMS2 SHARED ${AVS_SOURCES})
    TARGET_LINK_LIBRARIES(FFMS2 FFMS2Core ${FFMS2_LIBS})
    ADD_DEPENDENCIES(FFMS2 FFMS2Core)
    SET(FFMS2_LIB FFMS2)
ENDIF(AVS_SOURCES)

ADD_EXECUTABLE(ffmsindex src/index/ffmsindex.cpp)
TARGET_LINK_LIBRARIES(ffmsindex ${FFMS2_LIB} ${FFMS2_LIBS})

ADD_EXECUTABLE(ffmstest src/test/ffmstest.cpp)
TARGET_LINK_LIBRARIES(ffmstest ${FFMS2_LIB} ${FFMS2_LIBS})

ADD_DEPENDENCIES(ffmstest ${FFMS2_LIB})
ADD_DEPENDENCIES(ffmsindex ${FFMS2_LIB})
