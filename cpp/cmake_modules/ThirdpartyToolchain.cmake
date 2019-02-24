# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

add_custom_target(toolchain)
add_custom_target(toolchain-benchmarks)
add_custom_target(toolchain-tests)

# ----------------------------------------------------------------------
# Toolchain linkage options

set(ARROW_RE2_LINKAGE "static" CACHE STRING
  "How to link the re2 library. static|shared (default static)")

# ----------------------------------------------------------------------
# Thirdparty versions, environment variables, source URLs

set(THIRDPARTY_DIR "${arrow_SOURCE_DIR}/thirdparty")


if (NOT "$ENV{ARROW_BUILD_TOOLCHAIN}" STREQUAL "")
  set(BROTLI_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(BZ2_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(CARES_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(DOUBLE_CONVERSION_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(FLATBUFFERS_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(GFLAGS_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(GLOG_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(GRPC_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  # Using gtest from the toolchain breaks AppVeyor and
  # trusty builds
  if (NOT MSVC)
    if (APPLE)
      set(GTEST_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
    else()
      #linux
      execute_process(COMMAND lsb_release -cs
        OUTPUT_VARIABLE RELEASE_CODENAME
	OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if (NOT RELEASE_CODENAME STREQUAL "trusty")
	set(GTEST_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
      endif()
    endif()
  endif()
  set(JEMALLOC_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(LZ4_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  # orc disabled as it's not in conda-forge (but in Anaconda with an incompatible ABI)
  # set(ORC_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(PROTOBUF_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(RAPIDJSON_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(RE2_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(SNAPPY_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(THRIFT_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(ZLIB_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")
  set(ZSTD_HOME "$ENV{ARROW_BUILD_TOOLCHAIN}")

  if (NOT DEFINED ENV{BOOST_ROOT})
    # Since we have to set this in the environment, we check whether
    # $BOOST_ROOT is defined inside here
    set(ENV{BOOST_ROOT} "$ENV{ARROW_BUILD_TOOLCHAIN}")
  endif()
endif()

# Home path for each third-party lib can be overriden with env vars

if (DEFINED ENV{BROTLI_HOME})
  set(BROTLI_HOME "$ENV{BROTLI_HOME}")
endif()

if (DEFINED ENV{BZ2_HOME})
  set(BZ2_HOME "$ENV{BZ2_HOME}")
endif()

if (DEFINED ENV{CARES_HOME})
  set(CARES_HOME "$ENV{CARES_HOME}")
endif()

if (DEFINED ENV{DOUBLE_CONVERSION_HOME})
  set(DOUBLE_CONVERSION_HOME "$ENV{DOUBLE_CONVERSION_HOME}")
endif()

if (DEFINED ENV{FLATBUFFERS_HOME})
  set(FLATBUFFERS_HOME "$ENV{FLATBUFFERS_HOME}")
endif()

if (DEFINED ENV{GFLAGS_HOME})
  set(GFLAGS_HOME "$ENV{GFLAGS_HOME}")
endif()

if (DEFINED ENV{GLOG_HOME})
  set(GLOG_HOME "$ENV{GLOG_HOME}")
endif()

if (DEFINED ENV{GRPC_HOME})
  set(GRPC_HOME "$ENV{GRPC_HOME}")
endif()

if (DEFINED ENV{GTEST_HOME})
  set(GTEST_HOME "$ENV{GTEST_HOME}")
endif()

if (DEFINED ENV{JEMALLOC_HOME})
  set(JEMALLOC_HOME "$ENV{JEMALLOC_HOME}")
endif()

if (DEFINED ENV{LZ4_HOME})
  set(LZ4_HOME "$ENV{LZ4_HOME}")
endif()

if (DEFINED ENV{ORC_HOME})
  set(ORC_HOME "$ENV{ORC_HOME}")
endif()

if (DEFINED ENV{PROTOBUF_HOME})
  set(PROTOBUF_HOME "$ENV{PROTOBUF_HOME}")
endif()

if (DEFINED ENV{RAPIDJSON_HOME})
  set(RAPIDJSON_HOME "$ENV{RAPIDJSON_HOME}")
endif()

if (DEFINED ENV{RE2_HOME})
  set(RE2_HOME "$ENV{RAPIDJSON_HOME}")
endif()

if (DEFINED ENV{SNAPPY_HOME})
  set(SNAPPY_HOME "$ENV{SNAPPY_HOME}")
endif()

if (DEFINED ENV{THRIFT_HOME})
  set(THRIFT_HOME "$ENV{THRIFT_HOME}")
endif()

if (DEFINED ENV{ZLIB_HOME})
  set(ZLIB_HOME "$ENV{ZLIB_HOME}")
endif()

if (DEFINED ENV{ZSTD_HOME})
  set(ZSTD_HOME "$ENV{ZSTD_HOME}")
endif()

# ----------------------------------------------------------------------
# Some EP's require other EP's

if (ARROW_THRIFT OR ARROW_WITH_ZLIB)
  set(ARROW_WITH_ZLIB ON)
endif()

if (ARROW_HIVESERVER2 OR ARROW_PARQUET)
  set(ARROW_WITH_THRIFT ON)
else()
  set(ARROW_WITH_THRIFT OFF)
endif()

if (ARROW_FLIGHT)
  set(ARROW_WITH_GRPC ON)
endif()

if (ARROW_FLIGHT OR ARROW_IPC)
  set(ARROW_WITH_RAPIDJSON ON)
endif()

if (ARROW_ORC OR ARROW_FLIGHT OR ARROW_GANDIVA)
  set(ARROW_WITH_PROTOBUF ON)
endif()

# ----------------------------------------------------------------------
# Versions and URLs for toolchain builds, which also can be used to configure
# offline builds

# Read toolchain versions from cpp/thirdparty/versions.txt
file(STRINGS "${THIRDPARTY_DIR}/versions.txt" TOOLCHAIN_VERSIONS_TXT)
foreach(_VERSION_ENTRY ${TOOLCHAIN_VERSIONS_TXT})
  # Exclude comments
  if(NOT _VERSION_ENTRY MATCHES "^[^#][A-Za-z0-9-_]+_VERSION=")
    continue()
  endif()

  string(REGEX MATCH "^[^=]*" _LIB_NAME ${_VERSION_ENTRY})
  string(REPLACE "${_LIB_NAME}=" "" _LIB_VERSION ${_VERSION_ENTRY})

  # Skip blank or malformed lines
  if(${_LIB_VERSION} STREQUAL "")
    continue()
  endif()

  # For debugging
  message(STATUS "${_LIB_NAME}: ${_LIB_VERSION}")

  set(${_LIB_NAME} "${_LIB_VERSION}")
endforeach()

if (DEFINED ENV{ARROW_BOOST_URL})
  set(BOOST_SOURCE_URL "$ENV{ARROW_BOOST_URL}")
else()
  string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_VERSION})
  set(BOOST_SOURCE_URL
    "https://dl.bintray.com/boostorg/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORES}.tar.gz")
endif()

if (DEFINED ENV{ARROW_BROTLI_URL})
  set(BROTLI_SOURCE_URL "$ENV{ARROW_BROTLI_URL}")
else()
  set(BROTLI_SOURCE_URL "https://github.com/google/brotli/archive/${BROTLI_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_CARES_URL})
  set(CARES_SOURCE_URL "$ENV{ARROW_CARES_URL}")
else()
  set(CARES_SOURCE_URL "https://c-ares.haxx.se/download/c-ares-${CARES_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_DOUBLE_CONVERSION_URL})
  set(DOUBLE_CONVERSION_SOURCE_URL "$ENV{ARROW_DOUBLE_CONVERSION_URL}")
else()
  set(DOUBLE_CONVERSION_SOURCE_URL "https://github.com/google/double-conversion/archive/${DOUBLE_CONVERSION_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_FLATBUFFERS_URL})
  set(FLATBUFFERS_SOURCE_URL "$ENV{ARROW_FLATBUFFERS_URL}")
else()
  set(FLATBUFFERS_SOURCE_URL "https://github.com/google/flatbuffers/archive/${FLATBUFFERS_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_GBENCHMARK_URL})
  set(GBENCHMARK_SOURCE_URL "$ENV{ARROW_GBENCHMARK_URL}")
else()
  set(GBENCHMARK_SOURCE_URL "https://github.com/google/benchmark/archive/${GBENCHMARK_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_GFLAGS_URL})
  set(GFLAGS_SOURCE_URL "$ENV{ARROW_GFLAGS_URL}")
else()
  set(GFLAGS_SOURCE_URL "https://github.com/gflags/gflags/archive/${GFLAGS_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_GLOG_URL})
  set(GLOG_SOURCE_URL "$ENV{ARROW_GLOG_URL}")
else()
  set(GLOG_SOURCE_URL "https://github.com/google/glog/archive/${GLOG_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_GRPC_URL})
  set(GRPC_SOURCE_URL "$ENV{ARROW_GRPC_URL}")
else()
  set(GRPC_SOURCE_URL "https://github.com/grpc/grpc/archive/${GRPC_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_GTEST_URL})
  set(GTEST_SOURCE_URL "$ENV{ARROW_GTEST_URL}")
else()
  set(GTEST_SOURCE_URL "https://github.com/google/googletest/archive/release-${GTEST_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_LZ4_URL})
  set(LZ4_SOURCE_URL "$ENV{ARROW_LZ4_URL}")
else()
  set(LZ4_SOURCE_URL "https://github.com/lz4/lz4/archive/${LZ4_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_ORC_URL})
  set(ORC_SOURCE_URL "$ENV{ARROW_ORC_URL}")
else()
  set(ORC_SOURCE_URL "https://github.com/apache/orc/archive/rel/release-${ORC_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_PROTOBUF_URL})
  set(PROTOBUF_SOURCE_URL "$ENV{ARROW_PROTOBUF_URL}")
else()
  string(SUBSTRING ${PROTOBUF_VERSION} 1 -1 STRIPPED_PROTOBUF_VERSION)  # strip the leading `v`
  set(PROTOBUF_SOURCE_URL "https://github.com/protocolbuffers/protobuf/releases/download/${PROTOBUF_VERSION}/protobuf-all-${STRIPPED_PROTOBUF_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_RE2_URL})
  set(RE2_SOURCE_URL "$ENV{ARROW_RE2_URL}")
else()
  set(RE2_SOURCE_URL "https://github.com/google/re2/archive/${RE2_VERSION}.tar.gz")
endif()

set(RAPIDJSON_SOURCE_MD5 "badd12c511e081fec6c89c43a7027bce")
if (DEFINED ENV{ARROW_RAPIDJSON_URL})
  set(RAPIDJSON_SOURCE_URL "$ENV{ARROW_RAPIDJSON_URL}")
else()
  set(RAPIDJSON_SOURCE_URL "https://github.com/miloyip/rapidjson/archive/${RAPIDJSON_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_SNAPPY_URL})
  set(SNAPPY_SOURCE_URL "$ENV{ARROW_SNAPPY_URL}")
else()
  set(SNAPPY_SOURCE_URL "https://github.com/google/snappy/releases/download/${SNAPPY_VERSION}/snappy-${SNAPPY_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_THRIFT_URL})
  set(THRIFT_SOURCE_URL "$ENV{ARROW_THRIFT_URL}")
else()
  set(THRIFT_SOURCE_URL "http://archive.apache.org/dist/thrift/${THRIFT_VERSION}/thrift-${THRIFT_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_ZLIB_URL})
  set(ZLIB_SOURCE_URL "$ENV{ARROW_ZLIB_URL}")
else()
  set(ZLIB_SOURCE_URL "http://zlib.net/fossils/zlib-${ZLIB_VERSION}.tar.gz")
endif()

if (DEFINED ENV{ARROW_ZSTD_URL})
  set(ZSTD_SOURCE_URL "$ENV{ARROW_ZSTD_URL}")
else()
  set(ZSTD_SOURCE_URL "https://github.com/facebook/zstd/archive/${ZSTD_VERSION}.tar.gz")
endif()

# ----------------------------------------------------------------------
# ExternalProject options

string(TOUPPER ${CMAKE_BUILD_TYPE} UPPERCASE_BUILD_TYPE)

set(EP_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UPPERCASE_BUILD_TYPE}}")
set(EP_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${UPPERCASE_BUILD_TYPE}}")

if (NOT MSVC)
  # Set -fPIC on all external projects
  set(EP_CXX_FLAGS "${EP_CXX_FLAGS} -fPIC")
  set(EP_C_FLAGS "${EP_C_FLAGS} -fPIC")
endif()

# CC/CXX environment variables are captured on the first invocation of the
# builder (e.g make or ninja) instead of when CMake is invoked into to build
# directory. This leads to issues if the variables are exported in a subshell
# and the invocation of make/ninja is in distinct subshell without the same
# environment (CC/CXX).
set(EP_COMMON_CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                         -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})

# External projects are still able to override the following declarations.
# cmake command line will favor the last defined variable when a duplicate is
# encountered. This requires that `EP_COMMON_CMAKE_ARGS` is always the first
# argument.
set(EP_COMMON_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
                         -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                         -DCMAKE_C_FLAGS=${EP_C_FLAGS}
                         -DCMAKE_C_FLAGS_${UPPERCASE_BUILD_TYPE}=${EP_C_FLAGS}
                         -DCMAKE_CXX_FLAGS=${EP_CXX_FLAGS}
                         -DCMAKE_CXX_FLAGS_${UPPERCASE_BUILD_TYPE}=${EP_CXX_FLAGS})

if (CMAKE_AR)
  set(EP_COMMON_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
                           -DCMAKE_AR=${CMAKE_AR})
endif()

if (CMAKE_RANLIB)
  set(EP_COMMON_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
                           -DCMAKE_RANLIB=${CMAKE_RANLIB})
endif()

if (NOT ARROW_VERBOSE_THIRDPARTY_BUILD)
  set(EP_LOG_OPTIONS
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    LOG_DOWNLOAD 1)
  set(Boost_DEBUG FALSE)
else()
  set(EP_LOG_OPTIONS)
  set(Boost_DEBUG TRUE)
endif()

# Ensure that a default make is set
if ("${MAKE}" STREQUAL "")
    if (NOT MSVC)
        find_program(MAKE make)
    endif()
endif()

# Using make -j in sub-make is fragile
# see discussion https://github.com/apache/arrow/pull/2779
if (${CMAKE_GENERATOR} MATCHES "Makefiles")
    set(MAKE_BUILD_ARGS "")
else()
    # limit the maximum number of jobs for ninja
    set(MAKE_BUILD_ARGS "-j4")
endif()

# ----------------------------------------------------------------------
# Find pthreads

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ----------------------------------------------------------------------
# Add Boost dependencies (code adapted from Apache Kudu (incubating))

set(Boost_USE_MULTITHREADED ON)
if (MSVC AND ARROW_USE_STATIC_CRT)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()
set(Boost_ADDITIONAL_VERSIONS
  "1.70.0" "1.70"
  "1.69.0" "1.69"
  "1.68.0" "1.68"
  "1.67.0" "1.67"
  "1.66.0" "1.66"
  "1.65.0" "1.65"
  "1.64.0" "1.64"
  "1.63.0" "1.63"
  "1.62.0" "1.61"
  "1.61.0" "1.62"
  "1.60.0" "1.60")

if (ARROW_BOOST_VENDORED)
  set(BOOST_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/boost_ep-prefix/src/boost_ep")
  set(BOOST_LIB_DIR "${BOOST_PREFIX}/stage/lib")
  set(BOOST_BUILD_LINK "static")
  set(BOOST_STATIC_SYSTEM_LIBRARY
    "${BOOST_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}boost_system${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(BOOST_STATIC_FILESYSTEM_LIBRARY
    "${BOOST_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}boost_filesystem${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(BOOST_STATIC_REGEX_LIBRARY
    "${BOOST_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}boost_regex${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(BOOST_SYSTEM_LIBRARY boost_system_static)
  set(BOOST_FILESYSTEM_LIBRARY boost_filesystem_static)
  set(BOOST_REGEX_LIBRARY boost_regex_static)

  if (ARROW_BOOST_HEADER_ONLY)
    set(BOOST_BUILD_PRODUCTS)
    set(BOOST_CONFIGURE_COMMAND "")
    set(BOOST_BUILD_COMMAND "")
  else()
    set(BOOST_BUILD_PRODUCTS
      ${BOOST_STATIC_SYSTEM_LIBRARY}
      ${BOOST_STATIC_FILESYSTEM_LIBRARY}
      ${BOOST_STATIC_REGEX_LIBRARY})
    set(BOOST_CONFIGURE_COMMAND
      "./bootstrap.sh"
      "--prefix=${BOOST_PREFIX}"
      "--with-libraries=filesystem,regex,system")
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
      set(BOOST_BUILD_VARIANT "debug")
    else()
      set(BOOST_BUILD_VARIANT "release")
    endif()
    set(BOOST_BUILD_COMMAND
      "./b2"
      "link=${BOOST_BUILD_LINK}"
      "variant=${BOOST_BUILD_VARIANT}"
      "cxxflags=-fPIC")
  endif()
  ExternalProject_Add(boost_ep
    URL ${BOOST_SOURCE_URL}
    BUILD_BYPRODUCTS ${BOOST_BUILD_PRODUCTS}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ${BOOST_CONFIGURE_COMMAND}
    BUILD_COMMAND ${BOOST_BUILD_COMMAND}
    INSTALL_COMMAND ""
    ${EP_LOG_OPTIONS})
  set(Boost_INCLUDE_DIR "${BOOST_PREFIX}")
  set(Boost_INCLUDE_DIRS "${BOOST_INCLUDE_DIR}")
  add_dependencies(toolchain boost_ep)
else()
  if (MSVC)
    # disable autolinking in boost
    add_definitions(-DBOOST_ALL_NO_LIB)
  endif()

  if (DEFINED ENV{BOOST_ROOT} OR DEFINED BOOST_ROOT)
    # In older versions of CMake (such as 3.2), the system paths for Boost will
    # be looked in first even if we set $BOOST_ROOT or pass -DBOOST_ROOT
    set(Boost_NO_SYSTEM_PATHS ON)
  endif()

  if (ARROW_BOOST_USE_SHARED)
    # Find shared Boost libraries.
    set(Boost_USE_STATIC_LIBS OFF)

    if (MSVC)
      # force all boost libraries to dynamic link
      add_definitions(-DBOOST_ALL_DYN_LINK)
    endif()

    if (ARROW_BOOST_HEADER_ONLY)
      find_package(Boost REQUIRED)
    else()
      find_package(Boost COMPONENTS regex system filesystem REQUIRED)
      if ("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
        set(BOOST_SHARED_SYSTEM_LIBRARY ${Boost_SYSTEM_LIBRARY_DEBUG})
        set(BOOST_SHARED_FILESYSTEM_LIBRARY ${Boost_FILESYSTEM_LIBRARY_DEBUG})
        set(BOOST_SHARED_REGEX_LIBRARY ${Boost_REGEX_LIBRARY_DEBUG})
      else()
        set(BOOST_SHARED_SYSTEM_LIBRARY ${Boost_SYSTEM_LIBRARY_RELEASE})
        set(BOOST_SHARED_FILESYSTEM_LIBRARY ${Boost_FILESYSTEM_LIBRARY_RELEASE})
        set(BOOST_SHARED_REGEX_LIBRARY ${Boost_REGEX_LIBRARY_RELEASE})
      endif()
      set(BOOST_SYSTEM_LIBRARY boost_system_shared)
      set(BOOST_FILESYSTEM_LIBRARY boost_filesystem_shared)
      set(BOOST_REGEX_LIBRARY boost_regex_shared)
    endif()
  else()
    # Find static boost headers and libs
    # TODO Differentiate here between release and debug builds
    set(Boost_USE_STATIC_LIBS ON)
    if (ARROW_BOOST_HEADER_ONLY)
      find_package(Boost REQUIRED)
    else()
      find_package(Boost COMPONENTS regex system filesystem REQUIRED)
      if ("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
        set(BOOST_STATIC_SYSTEM_LIBRARY ${Boost_SYSTEM_LIBRARY_DEBUG})
        set(BOOST_STATIC_FILESYSTEM_LIBRARY ${Boost_FILESYSTEM_LIBRARY_DEBUG})
        set(BOOST_STATIC_REGEX_LIBRARY ${Boost_REGEX_LIBRARY_DEBUG})
      else()
        set(BOOST_STATIC_SYSTEM_LIBRARY ${Boost_SYSTEM_LIBRARY_RELEASE})
        set(BOOST_STATIC_FILESYSTEM_LIBRARY ${Boost_FILESYSTEM_LIBRARY_RELEASE})
        set(BOOST_STATIC_REGEX_LIBRARY ${Boost_REGEX_LIBRARY_RELEASE})
      endif()
      set(BOOST_SYSTEM_LIBRARY boost_system_static)
      set(BOOST_FILESYSTEM_LIBRARY boost_filesystem_static)
      set(BOOST_REGEX_LIBRARY boost_regex_static)
    endif()
  endif()
endif()

message(STATUS "Boost include dir: " ${Boost_INCLUDE_DIR})
message(STATUS "Boost libraries: " ${Boost_LIBRARIES})

if (NOT ARROW_BOOST_HEADER_ONLY)
  ADD_THIRDPARTY_LIB(boost_system
      STATIC_LIB "${BOOST_STATIC_SYSTEM_LIBRARY}"
      SHARED_LIB "${BOOST_SHARED_SYSTEM_LIBRARY}")

  ADD_THIRDPARTY_LIB(boost_filesystem
      STATIC_LIB "${BOOST_STATIC_FILESYSTEM_LIBRARY}"
      SHARED_LIB "${BOOST_SHARED_FILESYSTEM_LIBRARY}")

  ADD_THIRDPARTY_LIB(boost_regex
      STATIC_LIB "${BOOST_STATIC_REGEX_LIBRARY}"
      SHARED_LIB "${BOOST_SHARED_REGEX_LIBRARY}")

  SET(ARROW_BOOST_LIBS ${BOOST_SYSTEM_LIBRARY} ${BOOST_FILESYSTEM_LIBRARY})
endif()

include_directories(SYSTEM ${Boost_INCLUDE_DIR})

# ----------------------------------------------------------------------
# Google double-conversion

if("${DOUBLE_CONVERSION_HOME}" STREQUAL "")
  set(DOUBLE_CONVERSION_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/double-conversion_ep/src/double-conversion_ep")
  set(DOUBLE_CONVERSION_HOME "${DOUBLE_CONVERSION_PREFIX}")
  set(DOUBLE_CONVERSION_INCLUDE_DIR "${DOUBLE_CONVERSION_PREFIX}/include")
  set(DOUBLE_CONVERSION_STATIC_LIB "${DOUBLE_CONVERSION_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}double-conversion${CMAKE_STATIC_LIBRARY_SUFFIX}")

  set(DOUBLE_CONVERSION_CMAKE_ARGS
    ${EP_COMMON_CMAKE_ARGS}
    "-DCMAKE_INSTALL_PREFIX=${DOUBLE_CONVERSION_PREFIX}")

  ExternalProject_Add(double-conversion_ep
    ${EP_LOG_OPTIONS}
    INSTALL_DIR ${DOUBLE_CONVERSION_PREFIX}
    URL ${DOUBLE_CONVERSION_SOURCE_URL}
    CMAKE_ARGS ${DOUBLE_CONVERSION_CMAKE_ARGS}
    BUILD_BYPRODUCTS "${DOUBLE_CONVERSION_STATIC_LIB}")
  set(DOUBLE_CONVERSION_VENDORED 1)
  add_dependencies(toolchain double-conversion_ep)
else()
  find_package(double-conversion REQUIRED
    PATHS "${DOUBLE_CONVERSION_HOME}")
  set(DOUBLE_CONVERSION_VENDORED 0)
endif()

if (NOT DOUBLE_CONVERSION_VENDORED)
  get_property(DOUBLE_CONVERSION_STATIC_LIB TARGET double-conversion::double-conversion
    PROPERTY LOCATION)
  get_property(DOUBLE_CONVERSION_INCLUDE_DIR TARGET double-conversion::double-conversion
    PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
endif()

include_directories(SYSTEM ${DOUBLE_CONVERSION_INCLUDE_DIR})

ADD_THIRDPARTY_LIB(double-conversion
  STATIC_LIB ${DOUBLE_CONVERSION_STATIC_LIB})

message(STATUS "double-conversion include dir: ${DOUBLE_CONVERSION_INCLUDE_DIR}")
message(STATUS "double-conversion static library: ${DOUBLE_CONVERSION_STATIC_LIB}")

# ----------------------------------------------------------------------
# gflags

if (ARROW_BUILD_TESTS OR
    ARROW_BUILD_BENCHMARKS OR
    (ARROW_USE_GLOG AND GLOG_HOME) OR
    (ARROW_WITH_GRPC AND NOT GRPC_HOME))
  set(ARROW_NEED_GFLAGS 1)
else()
  set(ARROW_NEED_GFLAGS 0)
endif()

if(ARROW_NEED_GFLAGS)
  # gflags (formerly Googleflags) command line parsing
  find_package(GFlags)
  if(GFLAGS_FOUND)
    set(GFLAGS_VENDORED FALSE)
    get_filename_component(GFLAGS_HOME "${GFLAGS_INCLUDE_DIR}" DIRECTORY)
    if(ARROW_GFLAGS_USE_SHARED AND GFLAGS_SHARED)
      set(GFLAGS_LIBRARY gflags_shared)
    else()
      set(GFLAGS_LIBRARY gflags_static)
    endif()
  elseif(GFLAGS_HOME)
    message(FATAL_ERROR "No static or shared library provided for gflags: ${GFLAGS_HOME}")
  else()
    set(GFLAGS_VENDORED TRUE)
    set(GFLAGS_CMAKE_CXX_FLAGS ${EP_CXX_FLAGS})

    set(GFLAGS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gflags_ep-prefix/src/gflags_ep")
    set(GFLAGS_HOME "${GFLAGS_PREFIX}")
    set(GFLAGS_INCLUDE_DIR "${GFLAGS_PREFIX}/include")
    if(MSVC)
      set(GFLAGS_STATIC_LIB "${GFLAGS_PREFIX}/lib/gflags_static.lib")
    else()
      set(GFLAGS_STATIC_LIB "${GFLAGS_PREFIX}/lib/libgflags.a")
    endif()
    set(GFLAGS_VENDORED 1)
    set(GFLAGS_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${GFLAGS_PREFIX}"
                          -DBUILD_SHARED_LIBS=OFF
                          -DBUILD_STATIC_LIBS=ON
                          -DBUILD_PACKAGING=OFF
                          -DBUILD_TESTING=OFF
                          -DBUILD_CONFIG_TESTS=OFF
                          -DINSTALL_HEADERS=ON)

    ExternalProject_Add(gflags_ep
      URL ${GFLAGS_SOURCE_URL}
      ${EP_LOG_OPTIONS}
      BUILD_IN_SOURCE 1
      BUILD_BYPRODUCTS "${GFLAGS_STATIC_LIB}"
      CMAKE_ARGS ${GFLAGS_CMAKE_ARGS})

    add_dependencies(toolchain gflags_ep)

    message(STATUS "GFlags include dir: ${GFLAGS_INCLUDE_DIR}")
    message(STATUS "GFlags static library: ${GFLAGS_STATIC_LIB}")
      include_directories(SYSTEM ${GFLAGS_INCLUDE_DIR})
    ADD_THIRDPARTY_LIB(gflags
      STATIC_LIB ${GFLAGS_STATIC_LIB})
    set(GFLAGS_LIBRARY gflags_static)
    target_compile_definitions(${GFLAGS_LIBRARY} INTERFACE "GFLAGS_IS_A_DLL=0")
    if(MSVC)
      set_target_properties(${GFLAGS_LIBRARY}
	PROPERTIES
	INTERFACE_LINK_LIBRARIES "shlwapi.lib")
    endif()
    add_dependencies(${GFLAGS_LIBRARY} gflags_ep)
  endif()
endif()

# ----------------------------------------------------------------------
# Google gtest

if(ARROW_BUILD_TESTS OR ARROW_BUILD_BENCHMARKS)
  if("${GTEST_HOME}" STREQUAL "")
    set(GTEST_CMAKE_CXX_FLAGS ${EP_CXX_FLAGS})

    if(CMAKE_BUILD_TYPE MATCHES DEBUG)
      set(CMAKE_GTEST_DEBUG_EXTENSION "d")
    else()
      set(CMAKE_GTEST_DEBUG_EXTENSION "")
    endif()

    if(APPLE)
      set(GTEST_CMAKE_CXX_FLAGS ${GTEST_CMAKE_CXX_FLAGS}
                                -DGTEST_USE_OWN_TR1_TUPLE=1
                                -Wno-unused-value
                                -Wno-ignored-attributes)
    endif()

    set(GTEST_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/googletest_ep-prefix/src/googletest_ep")
    set(GTEST_HOME ${GTEST_PREFIX})
    set(GTEST_INCLUDE_DIR "${GTEST_PREFIX}/include")
    set(GTEST_STATIC_LIB
      "${GTEST_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_GTEST_DEBUG_EXTENSION}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GTEST_MAIN_STATIC_LIB
      "${GTEST_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main${CMAKE_GTEST_DEBUG_EXTENSION}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GTEST_VENDORED 1)
    set(GTEST_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${GTEST_PREFIX}"
      "-DCMAKE_INSTALL_LIBDIR=lib"
      -DCMAKE_CXX_FLAGS=${GTEST_CMAKE_CXX_FLAGS})
    set(GMOCK_INCLUDE_DIR "${GTEST_PREFIX}/include")
    set(GMOCK_STATIC_LIB
      "${GTEST_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gmock${CMAKE_GTEST_DEBUG_EXTENSION}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GMOCK_MAIN_STATIC_LIB
      "${GTEST_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gmock_main${CMAKE_GTEST_DEBUG_EXTENSION}${CMAKE_STATIC_LIBRARY_SUFFIX}")

    if (MSVC AND NOT ARROW_USE_STATIC_CRT)
      set(GTEST_CMAKE_ARGS ${GTEST_CMAKE_ARGS} -Dgtest_force_shared_crt=ON)
    endif()

    ExternalProject_Add(googletest_ep
      URL ${GTEST_SOURCE_URL}
      BUILD_BYPRODUCTS ${GTEST_STATIC_LIB} ${GTEST_MAIN_STATIC_LIB} ${GMOCK_STATIC_LIB} ${GMOCK_MAIN_STATIC_LIB}
      CMAKE_ARGS ${GTEST_CMAKE_ARGS}
      ${EP_LOG_OPTIONS})

    add_dependencies(toolchain-tests googletest_ep)
  else()
    find_package(GTest REQUIRED)
    set(GTEST_VENDORED 0)
  endif()

  message(STATUS "GTest include dir: ${GTEST_INCLUDE_DIR}")
  message(STATUS "GMock include dir: ${GMOCK_INCLUDE_DIR}")
  include_directories(SYSTEM ${GTEST_INCLUDE_DIR})
  if(GTEST_STATIC_LIB)
    message(STATUS "GTest static library: ${GTEST_STATIC_LIB}")
    message(STATUS "GMock static library: ${GMOCK_STATIC_LIB}")
    ADD_THIRDPARTY_LIB(gtest
      STATIC_LIB ${GTEST_STATIC_LIB})
    ADD_THIRDPARTY_LIB(gtest_main
      STATIC_LIB ${GTEST_MAIN_STATIC_LIB})
    ADD_THIRDPARTY_LIB(gmock
      STATIC_LIB ${GMOCK_STATIC_LIB})
    ADD_THIRDPARTY_LIB(gmock_main
      STATIC_LIB ${GMOCK_MAIN_STATIC_LIB})
    set(GTEST_LIBRARY gtest_static)
    set(GTEST_MAIN_LIBRARY gtest_main_static)
    set(GMOCK_LIBRARY gmock_static)
    set(GMOCK_MAIN_LIBRARY gmock_main_static)
  else()
    message(STATUS "GTest shared library: ${GTEST_SHARED_LIB}")
    message(STATUS "GMock shared library: ${GMOCK_SHARED_LIB}")
    ADD_THIRDPARTY_LIB(gtest
      SHARED_LIB ${GTEST_SHARED_LIB})
    ADD_THIRDPARTY_LIB(gtest_main
      SHARED_LIB ${GTEST_MAIN_SHARED_LIB})
    ADD_THIRDPARTY_LIB(gmock
      SHARED_LIB ${GMOCK_SHARED_LIB})
    ADD_THIRDPARTY_LIB(gmock_main
      SHARED_LIB ${GMOCK_MAIN_SHARED_LIB})
    set(GTEST_LIBRARY gtest_shared)
    set(GTEST_MAIN_LIBRARY gtest_main_shared)
    set(GMOCK_LIBRARY gmock_shared)
    set(GMOCK_MAIN_LIBRARY gmock_main_shared)
  endif()

  if(GTEST_VENDORED)
    add_dependencies(${GTEST_LIBRARY} googletest_ep)
    add_dependencies(${GTEST_MAIN_LIBRARY} googletest_ep)
    add_dependencies(${GMOCK_LIBRARY} googletest_ep)
    add_dependencies(${GMOCK_MAIN_LIBRARY} googletest_ep)
  endif()
endif()

if(ARROW_BUILD_BENCHMARKS)
  if("$ENV{GBENCHMARK_HOME}" STREQUAL "")
    if(CMAKE_VERSION VERSION_LESS 3.6)
      message(FATAL_ERROR "Building gbenchmark from source requires at least CMake 3.6")
    endif()

    if(NOT MSVC)
      set(GBENCHMARK_CMAKE_CXX_FLAGS "${EP_CXX_FLAGS} -std=c++11")
    endif()

    if(APPLE)
      set(GBENCHMARK_CMAKE_CXX_FLAGS "${GBENCHMARK_CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()

    set(GBENCHMARK_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gbenchmark_ep/src/gbenchmark_ep-install")
    set(GBENCHMARK_INCLUDE_DIR "${GBENCHMARK_PREFIX}/include")
    set(GBENCHMARK_STATIC_LIB "${GBENCHMARK_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GBENCHMARK_VENDORED 1)
    set(GBENCHMARK_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${GBENCHMARK_PREFIX}"
      -DBENCHMARK_ENABLE_TESTING=OFF
      -DCMAKE_CXX_FLAGS=${GBENCHMARK_CMAKE_CXX_FLAGS})
    if (APPLE)
      set(GBENCHMARK_CMAKE_ARGS ${GBENCHMARK_CMAKE_ARGS} "-DBENCHMARK_USE_LIBCXX=ON")
    endif()

    ExternalProject_Add(gbenchmark_ep
      URL ${GBENCHMARK_SOURCE_URL}
      BUILD_BYPRODUCTS "${GBENCHMARK_STATIC_LIB}"
      CMAKE_ARGS ${GBENCHMARK_CMAKE_ARGS}
      ${EP_LOG_OPTIONS})

    add_dependencies(toolchain-benchmarks gbenchmark_ep)
  else()
    find_package(GBenchmark REQUIRED)
    set(GBENCHMARK_VENDORED 0)
  endif()

  message(STATUS "GBenchmark include dir: ${GBENCHMARK_INCLUDE_DIR}")
  message(STATUS "GBenchmark static library: ${GBENCHMARK_STATIC_LIB}")
  include_directories(SYSTEM ${GBENCHMARK_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(gbenchmark
    STATIC_LIB ${GBENCHMARK_STATIC_LIB})

  if(GBENCHMARK_VENDORED)
    add_dependencies(gbenchmark_static gbenchmark_ep)
  endif()
endif()

if (ARROW_WITH_RAPIDJSON)
  # RapidJSON, header only dependency
  if("${RAPIDJSON_HOME}" STREQUAL "")
    set(RAPIDJSON_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/rapidjson_ep/src/rapidjson_ep-install")
    set(RAPIDJSON_HOME "${RAPIDJSON_PREFIX}")
    set(RAPIDJSON_CMAKE_ARGS
      -DRAPIDJSON_BUILD_DOC=OFF
      -DRAPIDJSON_BUILD_EXAMPLES=OFF
      -DRAPIDJSON_BUILD_TESTS=OFF
      "-DCMAKE_INSTALL_PREFIX=${RAPIDJSON_PREFIX}")

    ExternalProject_Add(rapidjson_ep
      ${EP_LOG_OPTIONS}
      PREFIX "${CMAKE_BINARY_DIR}"
      URL ${RAPIDJSON_SOURCE_URL}
      URL_MD5 ${RAPIDJSON_SOURCE_MD5}
      CMAKE_ARGS ${RAPIDJSON_CMAKE_ARGS})

    set(RAPIDJSON_INCLUDE_DIR "${RAPIDJSON_HOME}/include")
    set(RAPIDJSON_VENDORED 1)

    add_dependencies(toolchain rapidjson_ep)
  else()
    set(RAPIDJSON_INCLUDE_DIR "${RAPIDJSON_HOME}/include")
    set(RAPIDJSON_VENDORED 0)
  endif()
  message(STATUS "RapidJSON include dir: ${RAPIDJSON_INCLUDE_DIR}")
  include_directories(SYSTEM ${RAPIDJSON_INCLUDE_DIR})

  ## Flatbuffers
  if("${FLATBUFFERS_HOME}" STREQUAL "")
    set(FLATBUFFERS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/flatbuffers_ep-prefix/src/flatbuffers_ep-install")
    if (MSVC)
      set(FLATBUFFERS_CMAKE_CXX_FLAGS /EHsc)
    else()
      set(FLATBUFFERS_CMAKE_CXX_FLAGS -fPIC)
    endif()
    # We always need to do release builds, otherwise flatc will not be installed.
    ExternalProject_Add(flatbuffers_ep
      URL ${FLATBUFFERS_SOURCE_URL}
      CMAKE_ARGS
      ${EP_COMMON_CMAKE_ARGS}
      -DCMAKE_BUILD_TYPE=RELEASE
      "-DCMAKE_CXX_FLAGS=${FLATBUFFERS_CMAKE_CXX_FLAGS}"
      "-DCMAKE_INSTALL_PREFIX:PATH=${FLATBUFFERS_PREFIX}"
      "-DFLATBUFFERS_BUILD_TESTS=OFF"
      ${EP_LOG_OPTIONS})

    set(FLATBUFFERS_INCLUDE_DIR "${FLATBUFFERS_PREFIX}/include")
    set(FLATBUFFERS_COMPILER "${FLATBUFFERS_PREFIX}/bin/flatc")
    set(FLATBUFFERS_VENDORED 1)
    add_dependencies(toolchain flatbuffers_ep)
  else()
    find_package(Flatbuffers REQUIRED)
    set(FLATBUFFERS_VENDORED 0)
  endif()

  message(STATUS "Flatbuffers include dir: ${FLATBUFFERS_INCLUDE_DIR}")
  message(STATUS "Flatbuffers compiler: ${FLATBUFFERS_COMPILER}")
  include_directories(SYSTEM ${FLATBUFFERS_INCLUDE_DIR})
endif()
#----------------------------------------------------------------------

if (MSVC)
  # jemalloc is not supported on Windows
  set(ARROW_JEMALLOC off)
endif()

if (ARROW_JEMALLOC)
  # We only use a vendored jemalloc as we want to control its version.
  # Also our build of jemalloc is specially prefixed so that it will not
  # conflict with the default allocator as well as other jemalloc
  # installations.
  # find_package(jemalloc)

  set(ARROW_JEMALLOC_USE_SHARED OFF)
  set(JEMALLOC_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/jemalloc_ep-prefix/src/jemalloc_ep/dist/")
  set(JEMALLOC_HOME "${JEMALLOC_PREFIX}")
  set(JEMALLOC_INCLUDE_DIR "${JEMALLOC_PREFIX}/include")
  set(JEMALLOC_SHARED_LIB "${JEMALLOC_PREFIX}/lib/libjemalloc${CMAKE_SHARED_LIBRARY_SUFFIX}")
  set(JEMALLOC_STATIC_LIB "${JEMALLOC_PREFIX}/lib/libjemalloc_pic${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(JEMALLOC_VENDORED 1)
  # We need to disable TLS or otherwise C++ exceptions won't work anymore.
  ExternalProject_Add(jemalloc_ep
    URL ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/jemalloc/${JEMALLOC_VERSION}.tar.gz
    PATCH_COMMAND touch doc/jemalloc.3 doc/jemalloc.html
    CONFIGURE_COMMAND ./autogen.sh "AR=${CMAKE_AR}" "CC=${CMAKE_C_COMPILER}" "--prefix=${JEMALLOC_PREFIX}" "--with-jemalloc-prefix=je_arrow_" "--with-private-namespace=je_arrow_private_" "--disable-tls"
    ${EP_LOG_OPTIONS}
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE} ${MAKE_BUILD_ARGS}
    BUILD_BYPRODUCTS "${JEMALLOC_STATIC_LIB}" "${JEMALLOC_SHARED_LIB}"
    INSTALL_COMMAND ${MAKE} install)

  # Don't use the include directory directly so that we can point to a path
  # that is unique to our codebase.
  include_directories(SYSTEM "${CMAKE_CURRENT_BINARY_DIR}/jemalloc_ep-prefix/src/")

  ADD_THIRDPARTY_LIB(jemalloc
    STATIC_LIB ${JEMALLOC_STATIC_LIB}
    SHARED_LIB ${JEMALLOC_SHARED_LIB}
    DEPS Threads::Threads)
  add_dependencies(jemalloc_static jemalloc_ep)

  add_dependencies(toolchain jemalloc_ep)
endif()

## Google PerfTools
##
## Disabled with TSAN/ASAN as well as with gold+dynamic linking (see comment
## near definition of ARROW_USING_GOLD).
# find_package(GPerf REQUIRED)
# if (NOT "${ARROW_USE_ASAN}" AND
#     NOT "${ARROW_USE_TSAN}" AND
#     NOT ("${ARROW_USING_GOLD}" AND "${ARROW_LINK}" STREQUAL "d"))
#   ADD_THIRDPARTY_LIB(tcmalloc
#     STATIC_LIB "${TCMALLOC_STATIC_LIB}"
#     SHARED_LIB "${TCMALLOC_SHARED_LIB}")
#   ADD_THIRDPARTY_LIB(profiler
#     STATIC_LIB "${PROFILER_STATIC_LIB}"
#     SHARED_LIB "${PROFILER_SHARED_LIB}")
#   list(APPEND ARROW_BASE_LIBS tcmalloc profiler)
#   add_definitions("-DTCMALLOC_ENABLED")
#   set(ARROW_TCMALLOC_AVAILABLE 1)
# endif()

########################################################################
# HDFS thirdparty setup

if (DEFINED ENV{HADOOP_HOME})
  set(HADOOP_HOME $ENV{HADOOP_HOME})
  if (NOT EXISTS "${HADOOP_HOME}/include/hdfs.h")
    message(STATUS "Did not find hdfs.h in expected location, using vendored one")
    set(HADOOP_HOME "${THIRDPARTY_DIR}/hadoop")
  endif()
else()
  set(HADOOP_HOME "${THIRDPARTY_DIR}/hadoop")
endif()

set(HDFS_H_PATH "${HADOOP_HOME}/include/hdfs.h")
if (NOT EXISTS ${HDFS_H_PATH})
  message(FATAL_ERROR "Did not find hdfs.h at ${HDFS_H_PATH}")
endif()
message(STATUS "Found hdfs.h at: " ${HDFS_H_PATH})

include_directories(SYSTEM "${HADOOP_HOME}/include")

if (ARROW_WITH_ZLIB)
# ----------------------------------------------------------------------
# ZLIB

  if("${ZLIB_HOME}" STREQUAL "")
    find_package(ZLIB)
  else()
    find_package(ZLIB REQUIRED)
  endif()
  if(ZLIB_FOUND)
    ADD_THIRDPARTY_LIB(zlib SHARED_LIB ${ZLIB_SHARED_LIB})
    set(ZLIB_LIBRARY zlib_shared)
  else()
    set(ZLIB_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/zlib_ep/src/zlib_ep-install")
    set(ZLIB_HOME "${ZLIB_PREFIX}")
    set(ZLIB_INCLUDE_DIR "${ZLIB_PREFIX}/include")
    if (MSVC)
      if (${UPPERCASE_BUILD_TYPE} STREQUAL "DEBUG")
        set(ZLIB_STATIC_LIB_NAME zlibstaticd.lib)
      else()
        set(ZLIB_STATIC_LIB_NAME zlibstatic.lib)
      endif()
    else()
      set(ZLIB_STATIC_LIB_NAME libz.a)
    endif()
    set(ZLIB_STATIC_LIB "${ZLIB_PREFIX}/lib/${ZLIB_STATIC_LIB_NAME}")
    set(ZLIB_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${ZLIB_PREFIX}"
      -DBUILD_SHARED_LIBS=OFF)
    ADD_THIRDPARTY_LIB(zlib
      STATIC_LIB ${ZLIB_STATIC_LIB})
    set(ZLIB_LIBRARY zlib_static)

    ExternalProject_Add(zlib_ep
      URL ${ZLIB_SOURCE_URL}
      ${EP_LOG_OPTIONS}
      BUILD_BYPRODUCTS "${ZLIB_STATIC_LIB}"
      CMAKE_ARGS ${ZLIB_CMAKE_ARGS})
    add_dependencies(${ZLIB_LIBRARY} zlib_ep)

    add_dependencies(toolchain zlib_ep)
  endif()

  include_directories(SYSTEM ${ZLIB_INCLUDE_DIR})
endif()

if (ARROW_WITH_SNAPPY)
# ----------------------------------------------------------------------
# Snappy

  if("${SNAPPY_HOME}" STREQUAL "")
    set(SNAPPY_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/snappy_ep/src/snappy_ep-install")
    set(SNAPPY_HOME "${SNAPPY_PREFIX}")
    set(SNAPPY_INCLUDE_DIR "${SNAPPY_PREFIX}/include")
    if (MSVC)
      set(SNAPPY_STATIC_LIB_NAME snappy_static)
    else()
      set(SNAPPY_STATIC_LIB_NAME snappy)
    endif()
    set(SNAPPY_STATIC_LIB "${SNAPPY_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${SNAPPY_STATIC_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

    if (${UPPERCASE_BUILD_TYPE} EQUAL "RELEASE")
      if (APPLE)
        set(SNAPPY_CXXFLAGS "CXXFLAGS='-DNDEBUG -O1'")
      else()
        set(SNAPPY_CXXFLAGS "CXXFLAGS='-DNDEBUG -O2'")
      endif()
    endif()

    if (WIN32)
      set(SNAPPY_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_RANLIB=${CMAKE_RANLIB}
        "-DCMAKE_INSTALL_PREFIX=${SNAPPY_PREFIX}")
      set(SNAPPY_UPDATE_COMMAND ${CMAKE_COMMAND} -E copy
                        ${CMAKE_SOURCE_DIR}/cmake_modules/SnappyCMakeLists.txt
                        ./CMakeLists.txt &&
                        ${CMAKE_COMMAND} -E copy
                        ${CMAKE_SOURCE_DIR}/cmake_modules/SnappyConfig.h
                        ./config.h)
      ExternalProject_Add(snappy_ep
        UPDATE_COMMAND ${SNAPPY_UPDATE_COMMAND}
        ${EP_LOG_OPTIONS}
        BUILD_IN_SOURCE 1
        BUILD_COMMAND ${MAKE}
        INSTALL_DIR ${SNAPPY_PREFIX}
        URL ${SNAPPY_SOURCE_URL}
        CMAKE_ARGS ${SNAPPY_CMAKE_ARGS}
        BUILD_BYPRODUCTS "${SNAPPY_STATIC_LIB}")
    else()
      ExternalProject_Add(snappy_ep
        CONFIGURE_COMMAND ./configure --with-pic "AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}" "--prefix=${SNAPPY_PREFIX}" ${SNAPPY_CXXFLAGS}
        ${EP_LOG_OPTIONS}
        BUILD_IN_SOURCE 1
        BUILD_COMMAND ${MAKE}
        INSTALL_DIR ${SNAPPY_PREFIX}
        URL ${SNAPPY_SOURCE_URL}
        BUILD_BYPRODUCTS "${SNAPPY_STATIC_LIB}")
    endif()

    set(SNAPPY_VENDORED 1)
    add_dependencies(toolchain snappy_ep)
  else()
    find_package(Snappy REQUIRED)
    set(SNAPPY_VENDORED 0)
  endif()

  include_directories(SYSTEM ${SNAPPY_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(snappy
    STATIC_LIB ${SNAPPY_STATIC_LIB})

  if (SNAPPY_VENDORED)
    add_dependencies(snappy_static snappy_ep)
  endif()
endif()

if (ARROW_WITH_BROTLI)
# ----------------------------------------------------------------------
# Brotli

  if("${BROTLI_HOME}" STREQUAL "")
    set(BROTLI_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/brotli_ep/src/brotli_ep-install")
    set(BROTLI_HOME "${BROTLI_PREFIX}")
    set(BROTLI_INCLUDE_DIR "${BROTLI_PREFIX}/include")
    if (MSVC)
      set(BROTLI_LIB_DIR bin)
    else()
      set(BROTLI_LIB_DIR lib)
    endif()
    set(BROTLI_STATIC_LIBRARY_ENC "${BROTLI_PREFIX}/${BROTLI_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}brotlienc${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(BROTLI_STATIC_LIBRARY_DEC "${BROTLI_PREFIX}/${BROTLI_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}brotlidec${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(BROTLI_STATIC_LIBRARY_COMMON "${BROTLI_PREFIX}/${BROTLI_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}brotlicommon${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(BROTLI_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${BROTLI_PREFIX}"
      -DCMAKE_INSTALL_LIBDIR=lib
      -DBUILD_SHARED_LIBS=OFF)

    ExternalProject_Add(brotli_ep
      URL ${BROTLI_SOURCE_URL}
      BUILD_BYPRODUCTS "${BROTLI_STATIC_LIBRARY_ENC}" "${BROTLI_STATIC_LIBRARY_DEC}" "${BROTLI_STATIC_LIBRARY_COMMON}"
      ${BROTLI_BUILD_BYPRODUCTS}
      ${EP_LOG_OPTIONS}
      CMAKE_ARGS ${BROTLI_CMAKE_ARGS}
      STEP_TARGETS headers_copy)
    if (MSVC)
      ExternalProject_Get_Property(brotli_ep SOURCE_DIR)

      ExternalProject_Add_Step(brotli_ep headers_copy
        COMMAND xcopy /E /I include ..\\..\\..\\brotli_ep\\src\\brotli_ep-install\\include /Y
        DEPENDEES build
        WORKING_DIRECTORY ${SOURCE_DIR})
    endif()
    set(BROTLI_VENDORED 1)

    add_dependencies(toolchain brotli_ep)
  else()
    find_package(Brotli REQUIRED)
    set(BROTLI_VENDORED 0)
  endif()

  include_directories(SYSTEM ${BROTLI_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(brotli_enc
    STATIC_LIB ${BROTLI_STATIC_LIBRARY_ENC})
  ADD_THIRDPARTY_LIB(brotli_dec
    STATIC_LIB ${BROTLI_STATIC_LIBRARY_DEC})
  ADD_THIRDPARTY_LIB(brotli_common
    STATIC_LIB ${BROTLI_STATIC_LIBRARY_COMMON})

  if (BROTLI_VENDORED)
    add_dependencies(brotli_enc_static brotli_ep)
    add_dependencies(brotli_dec_static brotli_ep)
    add_dependencies(brotli_common_static brotli_ep)
  endif()
endif()

if (ARROW_WITH_BZ2)
# ----------------------------------------------------------------------
# BZ2

  if("${BZ2_HOME}" STREQUAL "")
    message(SEND_ERROR "a binary install of libbz2 must be present, please set the BZ2_HOME environment variable")
  else()
    find_package(Bz2 REQUIRED)
  endif()

  include_directories(SYSTEM ${BZ2_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(bz2
    STATIC_LIB ${BZ2_STATIC_LIB})
endif()

if (ARROW_WITH_LZ4)
# ----------------------------------------------------------------------
# Lz4

  if("${LZ4_HOME}" STREQUAL "")
    set(LZ4_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/lz4_ep-prefix/src/lz4_ep")
    set(LZ4_HOME "${LZ4_BUILD_DIR}")
    set(LZ4_INCLUDE_DIR "${LZ4_BUILD_DIR}/lib")

    if (MSVC)
      if (ARROW_USE_STATIC_CRT)
        if (${UPPERCASE_BUILD_TYPE} STREQUAL "DEBUG")
          set(LZ4_RUNTIME_LIBRARY_LINKAGE "/p:RuntimeLibrary=MultiThreadedDebug")
        else()
          set(LZ4_RUNTIME_LIBRARY_LINKAGE "/p:RuntimeLibrary=MultiThreaded")
        endif()
      endif()
      set(LZ4_STATIC_LIB "${LZ4_BUILD_DIR}/visual/VS2010/bin/x64_${CMAKE_BUILD_TYPE}/liblz4_static.lib")
      set(LZ4_BUILD_COMMAND BUILD_COMMAND msbuild.exe /m /p:Configuration=${CMAKE_BUILD_TYPE} /p:Platform=x64 /p:PlatformToolset=v140
                                          ${LZ4_RUNTIME_LIBRARY_LINKAGE} /t:Build ${LZ4_BUILD_DIR}/visual/VS2010/lz4.sln)
    else()
      set(LZ4_STATIC_LIB "${LZ4_BUILD_DIR}/lib/liblz4.a")
      set(LZ4_BUILD_COMMAND BUILD_COMMAND ${CMAKE_SOURCE_DIR}/build-support/build-lz4-lib.sh "AR=${CMAKE_AR}")
    endif()

    ExternalProject_Add(lz4_ep
        URL ${LZ4_SOURCE_URL}
        ${EP_LOG_OPTIONS}
        UPDATE_COMMAND ""
        ${LZ4_PATCH_COMMAND}
        CONFIGURE_COMMAND ""
        INSTALL_COMMAND ""
        BINARY_DIR ${LZ4_BUILD_DIR}
        BUILD_BYPRODUCTS ${LZ4_STATIC_LIB}
        ${LZ4_BUILD_COMMAND}
        )

    set(LZ4_VENDORED 1)

    add_dependencies(toolchain lz4_ep)
  else()
    find_package(Lz4 REQUIRED)
    set(LZ4_VENDORED 0)
  endif()

  include_directories(SYSTEM ${LZ4_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(lz4
    STATIC_LIB ${LZ4_STATIC_LIB})

  if (LZ4_VENDORED)
    add_dependencies(lz4_static lz4_ep)
  endif()
endif()

if (ARROW_WITH_ZSTD)
# ----------------------------------------------------------------------
# ZSTD

  if("${ZSTD_HOME}" STREQUAL "")
    set(ZSTD_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/zstd_ep-install")
    set(ZSTD_INCLUDE_DIR "${ZSTD_PREFIX}/include")

    set(ZSTD_CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      "-DCMAKE_INSTALL_PREFIX=${ZSTD_PREFIX}"
      -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
      -DZSTD_BUILD_PROGRAMS=off
      -DZSTD_BUILD_SHARED=off
      -DZSTD_BUILD_STATIC=on
      -DZSTD_MULTITHREAD_SUPPORT=off)

    if (MSVC)
      set(ZSTD_STATIC_LIB "${ZSTD_PREFIX}/${CMAKE_INSTALL_LIBDIR}/zstd_static.lib")
      if (ARROW_USE_STATIC_CRT)
        set(ZSTD_CMAKE_ARGS ${ZSTD_CMAKE_ARGS} "-DZSTD_USE_STATIC_RUNTIME=on")
      endif()
    else()
      set(ZSTD_STATIC_LIB "${ZSTD_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libzstd.a")
      # Only pass our C flags on Unix as on MSVC it leads to a
      # "incompatible command-line options" error
      set(ZSTD_CMAKE_ARGS ${ZSTD_CMAKE_ARGS}
                          -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                          -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                          -DCMAKE_C_FLAGS=${EP_C_FLAGS}
                          -DCMAKE_CXX_FLAGS=${EP_CXX_FLAGS})
    endif()

    if(CMAKE_VERSION VERSION_LESS 3.7)
      message(FATAL_ERROR "Building zstd using ExternalProject requires \
at least CMake 3.7")
    endif()

    ExternalProject_Add(zstd_ep
      ${EP_LOG_OPTIONS}
      CMAKE_ARGS ${ZSTD_CMAKE_ARGS}
      SOURCE_SUBDIR "build/cmake"
      INSTALL_DIR ${ZSTD_PREFIX}
      URL ${ZSTD_SOURCE_URL}
      BUILD_BYPRODUCTS "${ZSTD_STATIC_LIB}")

    set(ZSTD_VENDORED 1)
    add_dependencies(toolchain zstd_ep)
  else()
    find_package(ZSTD REQUIRED)
    set(ZSTD_VENDORED 0)
  endif()

  include_directories(SYSTEM ${ZSTD_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(zstd
    STATIC_LIB ${ZSTD_STATIC_LIB})

  if (ZSTD_VENDORED)
    add_dependencies(zstd_static zstd_ep)
  endif()
endif()

# ----------------------------------------------------------------------
# RE2 (required for Gandiva)
if (ARROW_GANDIVA)
  # re2
  if ("${RE2_HOME}" STREQUAL "")
    set (RE2_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/re2_ep-install")
    set (RE2_HOME "${RE2_PREFIX}")
    set (RE2_INCLUDE_DIR "${RE2_PREFIX}/include")
    set (RE2_STATIC_LIB "${RE2_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}re2${CMAKE_STATIC_LIBRARY_SUFFIX}")

    set(RE2_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${RE2_PREFIX}")

    ExternalProject_Add(re2_ep
      ${EP_LOG_OPTIONS}
      INSTALL_DIR ${RE2_PREFIX}
      URL ${RE2_SOURCE_URL}
      CMAKE_ARGS ${RE2_CMAKE_ARGS}
      BUILD_BYPRODUCTS "${RE2_STATIC_LIB}")
    set (RE2_VENDORED 1)
    add_dependencies(toolchain re2_ep)
  else ()
    find_package (RE2 REQUIRED)
    set (RE2_VENDORED 0)
  endif ()

  include_directories (SYSTEM ${RE2_INCLUDE_DIR})

  if (ARROW_RE2_LINKAGE STREQUAL "shared")
    ADD_THIRDPARTY_LIB(re2
      SHARED_LIB ${RE2_SHARED_LIB})
    set(RE2_LIBRARY re2_shared)
  else()
    ADD_THIRDPARTY_LIB(re2
      STATIC_LIB ${RE2_STATIC_LIB})
    set(RE2_LIBRARY re2_static)
  endif()
endif ()


# ----------------------------------------------------------------------
# Protocol Buffers (required for ORC and Flight and Gandiva libraries)

if (ARROW_WITH_PROTOBUF)
  # protobuf
  if ("${PROTOBUF_HOME}" STREQUAL "")
    set (PROTOBUF_PREFIX "${THIRDPARTY_DIR}/protobuf_ep-install")
    set (PROTOBUF_HOME "${PROTOBUF_PREFIX}")
    set (PROTOBUF_INCLUDE_DIR "${PROTOBUF_PREFIX}/include")
    set (PROTOBUF_STATIC_LIB "${PROTOBUF_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}protobuf${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set (PROTOBUF_EXECUTABLE "${PROTOBUF_PREFIX}/bin/protoc")
    set (PROTOBUF_CONFIGURE_ARGS "AR=${CMAKE_AR}"
                                 "RANLIB=${CMAKE_RANLIB}"
                                 "CC=${CMAKE_C_COMPILER}"
                                 "CXX=${CMAKE_CXX_COMPILER}"
                                 "--disable-shared"
                                 "--prefix=${PROTOBUF_PREFIX}"
                                 "CFLAGS=${EP_C_FLAGS}"
                                 "CXXFLAGS=${EP_CXX_FLAGS}")

    ExternalProject_Add(protobuf_ep
      CONFIGURE_COMMAND "./configure" ${PROTOBUF_CONFIGURE_ARGS}
      BUILD_IN_SOURCE 1
      URL ${PROTOBUF_SOURCE_URL}
      BUILD_BYPRODUCTS "${PROTOBUF_STATIC_LIB}" "${PROTOBUF_EXECUTABLE}"
      ${EP_LOG_OPTIONS})

    set (PROTOBUF_VENDORED 1)
    add_dependencies(toolchain protobuf_ep)
  else ()
    find_package (Protobuf REQUIRED)
    set (PROTOBUF_VENDORED 0)
  endif ()

  include_directories (SYSTEM ${PROTOBUF_INCLUDE_DIR})
  if (ARROW_PROTOBUF_USE_SHARED)
    ADD_THIRDPARTY_LIB(protobuf
      SHARED_LIB ${PROTOBUF_SHARED_LIB})
    set(PROTOBUF_LIBRARY protobuf_shared)
  else ()
    ADD_THIRDPARTY_LIB(protobuf
      STATIC_LIB ${PROTOBUF_STATIC_LIB})
    set(PROTOBUF_LIBRARY protobuf_static)
  endif ()
  if (PROTOBUF_VENDORED)
    add_dependencies (${PROTOBUF_LIBRARY} protobuf_ep)
  endif ()
endif()

# ----------------------------------------------------------------------
# Dependencies for Arrow Flight RPC

if (ARROW_WITH_GRPC)
  if ("${CARES_HOME}" STREQUAL "")
    set(CARES_VENDORED 1)
    set(CARES_PREFIX "${THIRDPARTY_DIR}/cares_ep-install")
    set(CARES_HOME "${CARES_PREFIX}")
    set(CARES_INCLUDE_DIR "${CARES_PREFIX}/include")

    # If you set -DCARES_SHARED=ON then the build system names the library
    # libcares_static.a
    set(CARES_STATIC_LIB "${CARES_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}cares${CMAKE_STATIC_LIBRARY_SUFFIX}")

    set(CARES_CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCARES_STATIC=ON
      -DCARES_SHARED=OFF
      "-DCMAKE_C_FLAGS=${EP_C_FLAGS}"
      "-DCMAKE_INSTALL_PREFIX=${CARES_PREFIX}")

    ExternalProject_Add(cares_ep
      ${EP_LOG_OPTIONS}
      URL ${CARES_SOURCE_URL}
      CMAKE_ARGS ${CARES_CMAKE_ARGS}
      BUILD_BYPRODUCTS "${CARES_STATIC_LIB}")

    add_dependencies(toolchain cares_ep)
  else()
    set(CARES_VENDORED 0)
    find_package(c-ares REQUIRED)
  endif()
  message(STATUS "c-ares library: ${CARES_STATIC_LIB}")

  add_custom_target(grpc)

  if ("${GRPC_HOME}" STREQUAL "")
    set(GRPC_VENDORED 1)
    set(GRPC_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/grpc_ep-prefix/src/grpc_ep-build")
    set(GRPC_PREFIX "${THIRDPARTY_DIR}/grpc_ep-install")
    set(GRPC_HOME "${GRPC_PREFIX}")
    set(GRPC_INCLUDE_DIR "${GRPC_PREFIX}/include")
    set(GRPC_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${GRPC_PREFIX}"
      -DBUILD_SHARED_LIBS=OFF)

    set(GRPC_STATIC_LIBRARY_GPR "${GRPC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gpr${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GRPC_STATIC_LIBRARY_GRPC "${GRPC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}grpc${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GRPC_STATIC_LIBRARY_GRPCPP "${GRPC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}grpc++${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GRPC_STATIC_LIBRARY_ADDRESS_SORTING "${GRPC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}address_sorting${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GRPC_CPP_PLUGIN "${GRPC_PREFIX}/bin/grpc_cpp_plugin")

    set(GRPC_CMAKE_PREFIX)

    add_custom_target(grpc_dependencies)

    if (CARES_VENDORED)
      add_dependencies(grpc_dependencies cares_ep)
    endif()

    if (GFLAGS_VENDORED)
      add_dependencies(grpc_dependencies gflags_ep)
    endif()

    if (PROTOBUF_VENDORED)
      add_dependencies(grpc_dependencies protobuf_ep)
    endif()

    set(GRPC_CMAKE_PREFIX "${GRPC_CMAKE_PREFIX};${CARES_HOME}")
    set(GRPC_CMAKE_PREFIX "${GRPC_CMAKE_PREFIX};${GFLAGS_HOME}")
    set(GRPC_CMAKE_PREFIX "${GRPC_CMAKE_PREFIX};${PROTOBUF_HOME}")

    # ZLIB is never vendored
    set(GRPC_CMAKE_PREFIX "${GRPC_CMAKE_PREFIX};${ZLIB_HOME}")

    if (RAPIDJSON_VENDORED)
      add_dependencies(grpc_dependencies rapidjson_ep)
    endif()

    # Yuck, see https://stackoverflow.com/a/45433229/776560
    string(REPLACE ";" "|" GRPC_PREFIX_PATH_ALT_SEP "${GRPC_CMAKE_PREFIX}")

    set(GRPC_CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_PREFIX_PATH='${GRPC_PREFIX_PATH_ALT_SEP}'
      -DgRPC_CARES_PROVIDER=package
      -DgRPC_GFLAGS_PROVIDER=package
      -DgRPC_PROTOBUF_PROVIDER=package
      -DgRPC_SSL_PROVIDER=package
      -DgRPC_ZLIB_PROVIDER=package
      -DCMAKE_CXX_FLAGS=${EP_CXX_FLAGS}
      -DCMAKE_C_FLAGS=${EP_C_FLAGS}
      -DCMAKE_INSTALL_PREFIX=${GRPC_PREFIX}
      -DCMAKE_INSTALL_LIBDIR=lib
      -DBUILD_SHARED_LIBS=OFF)

    # XXX the gRPC git checkout is huge and takes a long time
    # Ideally, we should be able to use the tarballs, but they don't contain
    # vendored dependencies such as c-ares...
    ExternalProject_Add(grpc_ep
      URL ${GRPC_SOURCE_URL}
      LIST_SEPARATOR |
      BUILD_BYPRODUCTS
        ${GRPC_STATIC_LIBRARY_GPR}
        ${GRPC_STATIC_LIBRARY_GRPC}
        ${GRPC_STATIC_LIBRARY_GRPCPP}
        ${GRPC_STATIC_LIBRARY_ADDRESS_SORTING}
        ${GRPC_CPP_PLUGIN}
      CMAKE_ARGS ${GRPC_CMAKE_ARGS}
      ${EP_LOG_OPTIONS})

    add_dependencies(grpc_ep grpc_dependencies)

    set(GPR_STATIC_LIB "${GRPC_STATIC_LIBRARY_GPR}")
    set(GRPC_STATIC_LIB "${GRPC_STATIC_LIBRARY_GRPC}")
    set(GRPCPP_STATIC_LIB "${GRPC_STATIC_LIBRARY_GRPCPP}")
    set(GRPC_ADDRESS_SORTING_STATIC_LIB "${GRPC_STATIC_LIBRARY_ADDRESS_SORTING}")

    add_dependencies(grpc grpc_ep)
    add_dependencies(toolchain grpc)
  else()
    find_package(gRPC REQUIRED)
    set(GRPC_VENDORED 0)
  endif()

  if ("${GRPC_CPP_PLUGIN}" STREQUAL "")
    message(SEND_ERROR "Please set GRPC_CPP_PLUGIN.")
  endif()

  include_directories(SYSTEM ${GRPC_INCLUDE_DIR})

  ADD_THIRDPARTY_LIB(grpc_gpr
    STATIC_LIB ${GPR_STATIC_LIB})

  ADD_THIRDPARTY_LIB(grpc_grpc
    STATIC_LIB ${GRPC_STATIC_LIB})

  ADD_THIRDPARTY_LIB(grpc_grpcpp
    STATIC_LIB ${GRPCPP_STATIC_LIB})

  ADD_THIRDPARTY_LIB(grpc_address_sorting
    STATIC_LIB ${GRPC_ADDRESS_SORTING_STATIC_LIB})

  ADD_THIRDPARTY_LIB(cares
    STATIC_LIB ${CARES_STATIC_LIB})
endif()

# ----------------------------------------------------------------------
# Apache ORC

if (ARROW_ORC)
  # orc
  if ("${ORC_HOME}" STREQUAL "")
    set(ORC_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/orc_ep-install")
    set(ORC_HOME "${ORC_PREFIX}")
    set(ORC_INCLUDE_DIR "${ORC_PREFIX}/include")
    set(ORC_STATIC_LIB "${ORC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}orc${CMAKE_STATIC_LIBRARY_SUFFIX}")

    if ("${COMPILER_FAMILY}" STREQUAL "clang")
      if ("${COMPILER_VERSION}" VERSION_GREATER "4.0")
        set(ORC_CMAKE_CXX_FLAGS " -Wno-zero-as-null-pointer-constant \
  -Wno-inconsistent-missing-destructor-override ")
      endif()
    endif()

    set(ORC_CMAKE_CXX_FLAGS "${EP_CXX_FLAGS} ${ORC_CMAKE_CXX_FLAGS}")

    # Since LZ4 isn't installed, the header file is in ${LZ4_HOME}/lib instead of
    # ${LZ4_HOME}/include, which forces us to specify the include directory
    # manually as well.
    set (ORC_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${ORC_PREFIX}"
      -DCMAKE_CXX_FLAGS=${ORC_CMAKE_CXX_FLAGS}
      -DBUILD_LIBHDFSPP=OFF
      -DBUILD_JAVA=OFF
      -DBUILD_TOOLS=OFF
      -DBUILD_CPP_TESTS=OFF
      -DINSTALL_VENDORED_LIBS=OFF
      -DPROTOBUF_HOME=${PROTOBUF_HOME}
      -DLZ4_HOME=${LZ4_HOME}
      -DLZ4_INCLUDE_DIR=${LZ4_INCLUDE_DIR}
      -DSNAPPY_HOME=${SNAPPY_HOME}
      -DZLIB_HOME=${ZLIB_HOME})

    ExternalProject_Add(orc_ep
      URL ${ORC_SOURCE_URL}
      BUILD_BYPRODUCTS ${ORC_STATIC_LIB}
      CMAKE_ARGS ${ORC_CMAKE_ARGS}
      ${EP_LOG_OPTIONS})

    add_dependencies(toolchain orc_ep)

    set(ORC_VENDORED 1)
    add_dependencies(orc_ep ${ZLIB_LIBRARY})
    if (LZ4_VENDORED)
      add_dependencies(orc_ep lz4_static)
    endif()
    if (SNAPPY_VENDORED)
      add_dependencies(orc_ep snappy_static)
    endif()
    if (PROTOBUF_VENDORED)
      add_dependencies(orc_ep ${PROTOBUF_LIBRARY})
    endif()
  else()
     set(ORC_INCLUDE_DIR "${ORC_HOME}/include")
     set(ORC_STATIC_LIB "${ORC_HOME}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}orc${CMAKE_STATIC_LIBRARY_SUFFIX}")
     set(ORC_VENDORED 0)
  endif()

  include_directories(SYSTEM ${ORC_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(orc
    STATIC_LIB ${ORC_STATIC_LIB}
    DEPS ${PROTOBUF_LIBRARY})

  if (ORC_VENDORED)
    add_dependencies(orc_static orc_ep)
  endif()
endif()

# ----------------------------------------------------------------------
# Thrift

if (ARROW_WITH_THRIFT)

# find thrift headers and libs
find_package(Thrift)

if (NOT THRIFT_FOUND)
  set(THRIFT_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/thrift_ep/src/thrift_ep-install")
  set(THRIFT_HOME "${THRIFT_PREFIX}")
  set(THRIFT_INCLUDE_DIR "${THRIFT_PREFIX}/include")
  set(THRIFT_COMPILER "${THRIFT_PREFIX}/bin/thrift")
  set(THRIFT_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
    "-DCMAKE_INSTALL_PREFIX=${THRIFT_PREFIX}"
    -DCMAKE_INSTALL_RPATH=${THRIFT_PREFIX}/lib
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_TUTORIALS=OFF
    -DWITH_QT4=OFF
    -DWITH_C_GLIB=OFF
    -DWITH_JAVA=OFF
    -DWITH_PYTHON=OFF
    -DWITH_HASKELL=OFF
    -DWITH_CPP=ON
    -DWITH_STATIC_LIB=ON
    -DWITH_LIBEVENT=OFF)

  # Thrift also uses boost. Forward important boost settings if there were ones passed.
  if (DEFINED BOOST_ROOT)
    set(THRIFT_CMAKE_ARGS ${THRIFT_CMAKE_ARGS} "-DBOOST_ROOT=${BOOST_ROOT}")
  endif()
  if (DEFINED Boost_NAMESPACE)
    set(THRIFT_CMAKE_ARGS ${THRIFT_CMAKE_ARGS} "-DBoost_NAMESPACE=${Boost_NAMESPACE}")
  endif()

  set(THRIFT_STATIC_LIB_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}thrift")
  if (MSVC)
    if (ARROW_USE_STATIC_CRT)
      set(THRIFT_STATIC_LIB_NAME "${THRIFT_STATIC_LIB_NAME}mt")
      set(THRIFT_CMAKE_ARGS ${THRIFT_CMAKE_ARGS} "-DWITH_MT=ON")
    else()
      set(THRIFT_STATIC_LIB_NAME "${THRIFT_STATIC_LIB_NAME}md")
      set(THRIFT_CMAKE_ARGS ${THRIFT_CMAKE_ARGS} "-DWITH_MT=OFF")
    endif()
  endif()
  if (${UPPERCASE_BUILD_TYPE} STREQUAL "DEBUG")
    set(THRIFT_STATIC_LIB_NAME "${THRIFT_STATIC_LIB_NAME}d")
  endif()
  set(THRIFT_STATIC_LIB "${THRIFT_PREFIX}/lib/${THRIFT_STATIC_LIB_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

  if (ZLIB_SHARED_LIB)
    set(THRIFT_CMAKE_ARGS "-DZLIB_LIBRARY=${ZLIB_SHARED_LIB}"
                          ${THRIFT_CMAKE_ARGS})
  else()
    set(THRIFT_CMAKE_ARGS "-DZLIB_LIBRARY=${ZLIB_STATIC_LIB}"
                          ${THRIFT_CMAKE_ARGS})
  endif()
  set(THRIFT_DEPENDENCIES ${THRIFT_DEPENDENCIES} ${ZLIB_LIBRARY})

  if (MSVC)
    set(WINFLEXBISON_VERSION 2.4.9)
    set(WINFLEXBISON_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/winflexbison_ep/src/winflexbison_ep-install")
    ExternalProject_Add(winflexbison_ep
      URL https://github.com/lexxmark/winflexbison/releases/download/v.${WINFLEXBISON_VERSION}/win_flex_bison-${WINFLEXBISON_VERSION}.zip
      URL_HASH MD5=a2e979ea9928fbf8567e995e9c0df765
      SOURCE_DIR ${WINFLEXBISON_PREFIX}
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      INSTALL_COMMAND ""
      ${EP_LOG_OPTIONS})
    set(THRIFT_DEPENDENCIES ${THRIFT_DEPENDENCIES} winflexbison_ep)

    set(THRIFT_CMAKE_ARGS "-DFLEX_EXECUTABLE=${WINFLEXBISON_PREFIX}/win_flex.exe"
                          "-DBISON_EXECUTABLE=${WINFLEXBISON_PREFIX}/win_bison.exe"
                          "-DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}"
                          "-DWITH_SHARED_LIB=OFF"
                          "-DWITH_PLUGIN=OFF"
                          ${THRIFT_CMAKE_ARGS})
  elseif (APPLE)
    # Some other process always resets BISON_EXECUTABLE to the system default,
    # thus we use our own variable here.
    if (NOT DEFINED THRIFT_BISON_EXECUTABLE)
      find_package(BISON 2.5.1)

      # In the case where we cannot find a system-wide installation, look for
      # homebrew and ask for its bison installation.
      if (NOT BISON_FOUND)
        find_program(BREW_BIN brew)
        if (BREW_BIN)
          execute_process(
            COMMAND ${BREW_BIN} --prefix bison
            OUTPUT_VARIABLE BISON_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
          )
          set(BISON_EXECUTABLE "${BISON_PREFIX}/bin/bison")
          find_package(BISON 2.5.1)
          set(THRIFT_BISON_EXECUTABLE "${BISON_EXECUTABLE}")
        endif()
      else()
        set(THRIFT_BISON_EXECUTABLE "${BISON_EXECUTABLE}")
      endif()
    endif()
    set(THRIFT_CMAKE_ARGS "-DBISON_EXECUTABLE=${THRIFT_BISON_EXECUTABLE}"
                          ${THRIFT_CMAKE_ARGS})
  endif()

  ExternalProject_Add(thrift_ep
    URL ${THRIFT_SOURCE_URL}
    BUILD_BYPRODUCTS "${THRIFT_STATIC_LIB}" "${THRIFT_COMPILER}"
    CMAKE_ARGS ${THRIFT_CMAKE_ARGS}
    DEPENDS ${THRIFT_DEPENDENCIES}
    ${EP_LOG_OPTIONS})

  set(THRIFT_VENDORED 1)

  add_dependencies(toolchain thrift_ep)
else()
  set(THRIFT_VENDORED 0)
endif()

include_directories(SYSTEM ${THRIFT_INCLUDE_DIR} ${THRIFT_INCLUDE_DIR}/thrift)
message(STATUS "Thrift include dir: ${THRIFT_INCLUDE_DIR}")
message(STATUS "Thrift static library: ${THRIFT_STATIC_LIB}")
message(STATUS "Thrift compiler: ${THRIFT_COMPILER}")
add_library(thriftstatic STATIC IMPORTED)
set_target_properties(thriftstatic PROPERTIES IMPORTED_LOCATION ${THRIFT_STATIC_LIB})

if (THRIFT_VENDORED)
  add_dependencies(thriftstatic thrift_ep)
endif()

if (THRIFT_VERSION VERSION_LESS "0.11.0")
  add_definitions(-DPARQUET_THRIFT_USE_BOOST)
  message(STATUS "Using Boost in Thrift header")
endif()

endif()  # ARROW_HIVESERVER2

# ----------------------------------------------------------------------
# GLOG

if (ARROW_USE_GLOG)
  if("${GLOG_HOME}" STREQUAL "")
    set(GLOG_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/glog_ep-prefix/src/glog_ep")
    set(GLOG_INCLUDE_DIR "${GLOG_BUILD_DIR}/include")
    set(GLOG_STATIC_LIB "${GLOG_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}glog${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(GLOG_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(GLOG_CMAKE_C_FLAGS "${EP_C_FLAGS} -fPIC")
    if (Threads::Threads)
      set(GLOG_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -pthread")
      set(GLOG_CMAKE_C_FLAGS "${EP_C_FLAGS} -fPIC -pthread")
    endif()
    message(STATUS "GLOG_CMAKE_CXX_FLAGS: ${GLOG_CMAKE_CXX_FLAGS}")
    message(STATUS "CMAKE_CXX_FLAGS in glog: ${GLOG_CMAKE_CXX_FLAGS}")

    if(APPLE)
      # If we don't set this flag, the binary built with 10.13 cannot be used in 10.12.
      set(GLOG_CMAKE_CXX_FLAGS "${GLOG_CMAKE_CXX_FLAGS} -mmacosx-version-min=10.9")
    endif()

    set(GLOG_CMAKE_ARGS ${EP_COMMON_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX=${GLOG_BUILD_DIR}"
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_TESTING=OFF
      -DWITH_GFLAGS=OFF
      -DCMAKE_CXX_FLAGS_${UPPERCASE_BUILD_TYPE}=${GLOG_CMAKE_CXX_FLAGS}
      -DCMAKE_C_FLAGS_${UPPERCASE_BUILD_TYPE}=${GLOG_CMAKE_C_FLAGS}
      -DCMAKE_CXX_FLAGS=${GLOG_CMAKE_CXX_FLAGS})
    message(STATUS "Glog version: ${GLOG_VERSION}")
    ExternalProject_Add(glog_ep
      URL ${GLOG_SOURCE_URL}
      BUILD_IN_SOURCE 1
      BUILD_BYPRODUCTS "${GLOG_STATIC_LIB}"
      CMAKE_ARGS ${GLOG_CMAKE_ARGS}
      ${EP_LOG_OPTIONS})

    set(GLOG_VENDORED 1)
    add_dependencies(toolchain glog_ep)
  else()
    find_package(GLOG REQUIRED)
    set(GLOG_VENDORED 0)
  endif()

  message(STATUS "Glog include dir: ${GLOG_INCLUDE_DIR}")
  message(STATUS "Glog static library: ${GLOG_STATIC_LIB}")

  include_directories(SYSTEM ${GLOG_INCLUDE_DIR})

  if (GLOG_VENDORED)
    ADD_THIRDPARTY_LIB(glog
      STATIC_LIB ${GLOG_STATIC_LIB})
    add_dependencies(glog_static glog_ep)
  else()
    ADD_THIRDPARTY_LIB(glog
      STATIC_LIB ${GLOG_STATIC_LIB}
      DEPS ${GFLAGS_LIBRARY})
  endif()
endif()
