# - Find opus library
# Find the native Opus headers and libraries. Opus depends on Ogg and will
# provide Ogg headers/libraries as well.
#
#  OPUS_INCLUDE_DIRS   - where to find opus/opus.h, ogg/ogg.h, etc
#  OPUS_LIBRARIES      - List of libraries when using libopus
#  OPUS_FOUND          - True if opus is found.


#=============================================================================
#Copyright 2000-2009 Kitware, Inc., Insight Software Consortium
#Copyright 2014, Mateusz "maxmati" Nowotynski
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#* Redistributions of source code must retain the above copyright notice,
#this list of conditions and the following disclaimer.
#
#* Redistributions in binary form must reproduce the above copyright notice,
#this list of conditions and the following disclaimer in the documentation
#and/or other materials provided with the distribution.
#
#* Neither the names of Kitware, Inc., the Insight Software Consortium, nor
#the names of their contributors may be used to endorse or promote products
#derived from this software without specific prior written  permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#POSSIBILITY OF SUCH DAMAGE.
#=============================================================================


# Look for the opusfile header file.
find_path( OPUS_INCLUDE_DIR
  NAMES opus/opus.h
  DOC "Opus include directory" )
mark_as_advanced( OPUS_INCLUDE_DIR )

# Look for the opus library.
find_library( OPUS_LIBRARY
  NAMES opus
  DOC "Path to Opus library" )
mark_as_advanced( OPUS_LIBRARY )


# handle the QUIETLY and REQUIRED arguments and set OPUSFILE_FOUND to TRUE if
# all listed variables are TRUE
include( ${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( Opus DEFAULT_MSG OPUS_LIBRARY OPUS_INCLUDE_DIR )

set( OPUS_LIBRARIES ${OPUS_LIBRARY})
set( OPUS_INCLUDE_DIRS ${OPUS_INCLUDE_DIR} ${OPUS_INCLUDE_DIR}/opus )
