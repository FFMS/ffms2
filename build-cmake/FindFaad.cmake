# Copyright (c) 2009, Whispersoft s.r.l.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
# * Neither the name of Whispersoft s.r.l. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Finds Faad library
#
#  Faad_INCLUDE_DIR - where to find faad.h, etc.
#  Faad_LIBRARIES   - List of libraries when using Faad.
#  Faad_FOUND       - True if faad found.
#

if (Faad_INCLUDE_DIR)
  # Already in cache, be silent
  set(Faad_FIND_QUIETLY TRUE)
endif (Faad_INCLUDE_DIR)

find_path(Faad_INCLUDE_DIR faad.h
  /opt/local/include
  /usr/local/include
  /usr/include
)

set(Faad_NAMES faad)
find_library(Faad_LIBRARY
  NAMES ${Faad_NAMES}
  PATHS /usr/lib /usr/local/lib /opt/local/lib
)

if (Faad_INCLUDE_DIR AND Faad_LIBRARY)
   set(Faad_FOUND TRUE)
   set( Faad_LIBRARIES ${Faad_LIBRARY} )
else (Faad_INCLUDE_DIR AND Faad_LIBRARY)
   set(Faad_FOUND FALSE)
   set(Faad_LIBRARIES)
endif (Faad_INCLUDE_DIR AND Faad_LIBRARY)

if (Faad_FOUND)
   if (NOT Faad_FIND_QUIETLY)
      message(STATUS "Found Faad: ${Faad_LIBRARY}")
   endif (NOT Faad_FIND_QUIETLY)
else (Faad_FOUND)
   if (Faad_FIND_REQUIRED)
      message(STATUS "Looked for Faad libraries named ${Faad_NAMES}.")
      message(STATUS "Include file detected: [${Faad_INCLUDE_DIR}].")
      message(STATUS "Lib file detected: [${Faad_LIBRARY}].")
      message(FATAL_ERROR "=========> Could NOT find Faad library")
   endif (Faad_FIND_REQUIRED)
endif (Faad_FOUND)

mark_as_advanced(
  Faad_LIBRARY
  Faad_INCLUDE_DIR
  )
