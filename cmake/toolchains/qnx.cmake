set(CMAKE_SYSTEM_NAME QNX)

set(CBOR_TAGS_QNX_ARCH
    "x86_64"
    CACHE STRING "QNX target architecture and QNX_TARGET sysroot subdirectory")
set_property(CACHE CBOR_TAGS_QNX_ARCH PROPERTY STRINGS x86_64 aarch64le)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CBOR_TAGS_QNX_ARCH)
list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
set(CMAKE_SYSTEM_PROCESSOR "${CBOR_TAGS_QNX_ARCH}")
set(qnx_compiler_target "gcc_nto${CBOR_TAGS_QNX_ARCH}")

foreach(qnx_environment_variable IN ITEMS QNX_HOST QNX_TARGET)
  if(NOT DEFINED ENV{${qnx_environment_variable}} OR "$ENV{${qnx_environment_variable}}" STREQUAL "")
    message(FATAL_ERROR "${qnx_environment_variable} is not set. Source qnxsdp-env.sh before configuring the QNX build.")
  endif()
endforeach()

file(TO_CMAKE_PATH "$ENV{QNX_HOST}" qnx_host)
file(TO_CMAKE_PATH "$ENV{QNX_TARGET}" qnx_target)
set(qnx_sysroot "${qnx_target}/${CBOR_TAGS_QNX_ARCH}")

if(NOT EXISTS "${qnx_host}/usr/bin/qcc" OR NOT EXISTS "${qnx_host}/usr/bin/q++")
  message(FATAL_ERROR "QNX_HOST does not contain usr/bin/qcc and usr/bin/q++: ${qnx_host}")
endif()
if(NOT IS_DIRECTORY "${qnx_sysroot}")
  message(FATAL_ERROR "QNX target sysroot does not exist: ${qnx_sysroot}")
endif()

set(CMAKE_C_COMPILER
    "${qnx_host}/usr/bin/qcc"
    CACHE FILEPATH "QNX C compiler")
set(CMAKE_C_COMPILER_TARGET
    "${qnx_compiler_target}"
    CACHE STRING "QCC C target" FORCE)
set(CMAKE_CXX_COMPILER
    "${qnx_host}/usr/bin/q++"
    CACHE FILEPATH "QNX C++ compiler")
set(CMAKE_CXX_COMPILER_TARGET
    "${qnx_compiler_target}"
    CACHE STRING "QCC C++ target" FORCE)
set(CMAKE_FIND_ROOT_PATH
    "${qnx_sysroot}"
    CACHE PATH "QNX target dependency root")

# Build tools execute on the host. Libraries, headers, and CMake packages must come from the QNX sysroot or be cross-built by FetchContent/CPM.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
    NEVER
    CACHE STRING "Find host build programs outside the QNX sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
    ONLY
    CACHE STRING "Find only QNX target libraries")
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
    ONLY
    CACHE STRING "Find only QNX target headers")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    ONLY
    CACHE STRING "Find only QNX target packages")
