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
FIND_PACKAGE(BZip2)
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
               src/core/audiosource.cpp src/core/haaliaudio.cpp
               src/core/lavfaudio.cpp src/core/matroskaaudio.cpp
               src/core/videosource.cpp src/core/haalivideo.cpp
               src/core/lavfvideo.cpp src/core/matroskavideo.cpp
               src/core/ffms.cpp src/core/wave64writer.cpp src/core/utils.cpp
               src/core/indexing.cpp src/core/haaliindexer.cpp
               src/core/lavfindexer.cpp src/core/matroskaindexer.cpp)


ADD_LIBRARY(FFMS2 SHARED ${FFMS2_SRCS})
TARGET_LINK_LIBRARIES(FFMS2 ${FFMS2_LIBS})

ADD_EXECUTABLE(ffmsindex src/index/ffmsindex.cpp)
TARGET_LINK_LIBRARIES(ffmsindex FFMS2 ${FFMS2_LIBS})

ADD_EXECUTABLE(ffmstest src/test/ffmstest.cpp)
TARGET_LINK_LIBRARIES(ffmstest FFMS2 ${FFMS2_LIBS})

ADD_DEPENDENCIES(ffmstest FFMS2)
ADD_DEPENDENCIES(ffmsindex FFMS2)

INSTALL(FILES ${CMAKE_HOME_DIRECTORY}/include/ffms.h DESTINATION include)

INSTALL(TARGETS FFMS2 ffmsindex ffmstest
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

