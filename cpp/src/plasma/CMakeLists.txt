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

cmake_minimum_required(VERSION 2.8)

project(plasma)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../python/cmake_modules")

find_package(PythonLibsNew REQUIRED)
find_package(Threads)

set(PLASMA_SO_VERSION "0")
set(PLASMA_ABI_VERSION "${PLASMA_SO_VERSION}.0.0")

include_directories(SYSTEM ${PYTHON_INCLUDE_DIRS})
include_directories("${FLATBUFFERS_INCLUDE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/" "${CMAKE_CURRENT_LIST_DIR}/thirdparty/" "${CMAKE_CURRENT_LIST_DIR}/../")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-conversion")

# Compile flatbuffers

set(PLASMA_FBS_SRC "${CMAKE_CURRENT_LIST_DIR}/format/plasma.fbs" "${CMAKE_CURRENT_LIST_DIR}/format/common.fbs")
set(OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/)

set(PLASMA_FBS_OUTPUT_FILES
  "${OUTPUT_DIR}/common_generated.h"
  "${OUTPUT_DIR}/plasma_generated.h")

add_custom_target(gen_plasma_fbs DEPENDS ${PLASMA_FBS_OUTPUT_FILES})

if(FLATBUFFERS_VENDORED)
  add_dependencies(gen_plasma_fbs flatbuffers_ep)
endif()

add_custom_command(
  OUTPUT ${PLASMA_FBS_OUTPUT_FILES}
  # The --gen-object-api flag generates a C++ class MessageT for each
  # flatbuffers message Message, which can be used to store deserialized
  # messages in data structures. This is currently used for ObjectInfo for
  # example.
  COMMAND ${FLATBUFFERS_COMPILER} -c -o ${OUTPUT_DIR} ${PLASMA_FBS_SRC} --gen-object-api
  DEPENDS ${PLASMA_FBS_SRC}
  COMMENT "Running flatc compiler on ${PLASMA_FBS_SRC}"
  VERBATIM)

if(UNIX AND NOT APPLE)
  link_libraries(rt)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

set(PLASMA_SRCS
  client.cc
  common.cc
  eviction_policy.cc
  events.cc
  fling.cc
  io.cc
  malloc.cc
  plasma.cc
  protocol.cc
  thirdparty/ae/ae.c
  thirdparty/xxhash.cc)

ADD_ARROW_LIB(plasma
  SOURCES ${PLASMA_SRCS}
  DEPENDENCIES gen_plasma_fbs
  SHARED_LINK_LIBS ${FLATBUFFERS_STATIC_LIB} ${CMAKE_THREAD_LIBS_INIT} arrow_static
  STATIC_LINK_LIBS ${FLATBUFFERS_STATIC_LIB} ${CMAKE_THREAD_LIBS_INIT} arrow_static)

# The optimization flag -O3 is suggested by dlmalloc.c, which is #included in
# malloc.cc; we set it here regardless of whether we do a debug or release build.
set_source_files_properties(malloc.cc PROPERTIES COMPILE_FLAGS "-Wno-error -O3")

add_executable(plasma_store store.cc)
target_link_libraries(plasma_store plasma_static)

# Headers: top level
install(FILES
  common.h
  common_generated.h
  client.h
  events.h
  plasma.h
  plasma_generated.h
  protocol.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/plasma")

# Plasma store
install(TARGETS plasma_store DESTINATION ${CMAKE_INSTALL_BINDIR})

# pkg-config support
configure_file(plasma.pc.in
  "${CMAKE_CURRENT_BINARY_DIR}/plasma.pc"
  @ONLY)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/plasma.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig/")

#######################################
# Unit tests
#######################################

ADD_ARROW_TEST(test/serialization_tests)
ARROW_TEST_LINK_LIBRARIES(test/serialization_tests plasma_static)
ADD_ARROW_TEST(test/client_tests)
ARROW_TEST_LINK_LIBRARIES(test/client_tests plasma_static)
