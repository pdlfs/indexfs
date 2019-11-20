# Copyright (c) 2019 Carnegie Mellon University,
# Copyright (c) 2019 Triad National Security, LLC, as operator of
#     Los Alamos National Laboratory.
# All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# with the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of CMU, TRIAD, Los Alamos National Laboratory, LANL, the
#    U.S. Government, nor the names of its contributors may be used to endorse
#    or promote products derived from this software without specific prior
#    written permission.
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# CMakeLists.txt  cmake file for libindexfs directory
# 16-Jun-2016  chuck@ece.cmu.edu
#

#
# this file is either included from ../CMakeLists.txt or some other
# file if we are being embedded within another project.
#

#
# list of source files and tests
#

# main directory sources and tests
set (indexfs-srcs indexfs_api.cc)
set (indexfs-tests indexfs_api_test.cc)

# configure/load in standard modules we plan to use
include (CMakePackageConfigHelpers)
set (CMAKE_THREAD_PREFER_PTHREAD TRUE)
set (THREADS_PREFER_PTHREAD_FLAG TRUE)

# note: cmake 3.1 and newer define a Threads::Threads imported target
# that we should switch to when we are ready to require 3.1 or better.
# cmake 3.1 was released late 2014.
find_package (Threads REQUIRED)

# common lib is always required
set (INDEXFS_REQUIRED_PACKAGES "indexfs-common")

# where the cmake package files get installed (for find_package)
set (ifs-pkg-loc "share/cmake/indexfs")

#
# library version set here (e.g. for generating shared libs if we want
#
set (INDEXFS_VERSION_MAJOR 1)
set (INDEXFS_VERSION_MINOR 0)
set (INDEXFS_VERSION_PATCH 0)
set (ifs-vers "${INDEXFS_VERSION_MAJOR}.${INDEXFS_VERSION_MINOR}")
set (INDEXFS_VERSION "${ifs-vers}.${INDEXFS_VERSION_PATCH}")

# setup and configure indexfs_config.h.in
configure_file ("../../include/indexfs/indexfs_config.h.in"
        "../../include/indexfs/indexfs_config.h" @ONLY)

#
# generate complete list of files and tests from the parts
#
set (indexfs-all-srcs ${indexfs-srcs})
set (indexfs-all-tests ${indexfs-tests})

#
# create the library target (user can specify shared vs. static
# using BUILD_SHARED_LIBS).   arrange for users of our lib to
# get the correct -Is for our include directories.  (for building
# we need a -I pointing to the build dir for pdlfs_config_expand.h)
#
add_library (indexfs ${indexfs-all-srcs})
target_include_directories (indexfs PUBLIC $<INSTALL_INTERFACE:include>)
target_include_directories (indexfs BEFORE PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/../../include>)
target_link_libraries (indexfs indexfs-common)
if (THREADS_HAVE_PTHREAD_ARG)
    target_compile_options(indexfs PUBLIC "-pthread")
endif()
if (CMAKE_THREAD_LIBS_INIT)
    target_link_libraries (indexfs "${CMAKE_THREAD_LIBS_INIT}")
endif ()

set_target_properties(indexfs PROPERTIES VERSION ${INDEXFS_VERSION}
        SOVERSION ${INDEXFS_VERSION_MAJOR})

#
#
# here's the compiler/machine/os dependent stuff...
# this likely isn't that useful (carryover from leveldb)
#

# "-pthread" should take care of all thread related options.
# but if we don't have it, fall back to -D_REENTRANT
if (NOT THREADS_HAVE_PTHREAD_ARG)
    target_compile_definitions (indexfs PUBLIC -D_REENTRANT)
endif ()

# XXX: untested solaris, does it need "-mt"?
# XXX: leveldb also added "-lrt" .. but is that really needed?
if (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
    CHECK_CXX_COMPILER_FLAG (-mt flag-mt)
    if (${flag-mt})
        target_compile_options (indexfs PUBLIC -mt)
    endif ()
endif ()

#
# end of the compiler/machine/os dependent stuff!
#

#
# installation stuff (packaging and install commands)
#
write_basic_package_version_file(
        "indexfs-config-version.cmake"
        VERSION ${INDEXFS_VERSION}
        COMPATIBILITY AnyNewerVersion)

# generate our config file for find_package()
configure_file (indexfs-config.cmake.in indexfs-config.cmake @ONLY)

# "make install" rules...
install (TARGETS indexfs EXPORT indexfs-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)
install (EXPORT indexfs-targets
        DESTINATION ${ifs-pkg-loc})
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/indexfs-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/indexfs-config-version.cmake"
        DESTINATION ${ifs-pkg-loc})
install (DIRECTORY ../../include/indexfs
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")
install (FILES
        "${CMAKE_CURRENT_BINARY_DIR}/../../include/indexfs/indexfs_config.h"
        DESTINATION include/indexfs)

#
# tests... we EXCLUDE_FROM_ALL the tests and use pdlfs-options.cmake's
# pdl-build-tests target for building.
#
foreach (lcv ${indexfs-all-tests})

    # use basename as test name
    get_filename_component (id ${lcv} NAME_WE)

    add_executable (${id} EXCLUDE_FROM_ALL ${lcv})
    target_link_libraries (${id} indexfs)
    add_test (${id} ${id})
    add_dependencies (pdl-build-tests ${id})

endforeach ()
