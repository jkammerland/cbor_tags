message(STATUS "Current cmake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.22)

macro(cbor_tags_append_flag variable)
  set(value "${${variable}}")
  foreach(flag ${ARGN})
    string(FIND " ${value} " " ${flag} " flag_index)
    if(flag_index EQUAL -1)
      if(value STREQUAL "")
        set(value "${flag}")
      else()
        string(APPEND value " ${flag}")
      endif()
    endif()
  endforeach()
  set(${variable} "${value}")
  unset(flag)
  unset(flag_index)
  unset(value)
endmacro()

macro(cbor_tags_append_optimization_flag variable flag)
  if(NOT " ${${variable}} " MATCHES " [/-]O([0-3sgz]|d|x|fast)( |$)")
    cbor_tags_append_flag(${variable} ${flag})
  endif()
endmacro()

if(CMAKE_GENERATOR)
  message(STATUS "Using generator [cmake ... -G ${CMAKE_GENERATOR} ...]")
else() # Print active generator
  message(STATUS "No generator set?")
endif()

cbor_tags_append_flag(CMAKE_CXX_FLAGS -Wall -Wextra -pedantic)

# Shared flags for all compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    cbor_tags_append_optimization_flag(CMAKE_CXX_FLAGS_DEBUG -O0) # -fconcepts-diagnostics-depth=2 will not work with tidy
    cbor_tags_append_flag(CMAKE_CXX_FLAGS_DEBUG -g -DDEBUG)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    cbor_tags_append_optimization_flag(CMAKE_CXX_FLAGS_DEBUG -O0)
    cbor_tags_append_flag(CMAKE_CXX_FLAGS_DEBUG -g -DDEBUG)
  endif()

  cbor_tags_append_optimization_flag(CMAKE_CXX_FLAGS_RELEASE -O3)
  cbor_tags_append_flag(CMAKE_CXX_FLAGS_RELEASE -DNDEBUG)

elseif(MSVC)
  message(STATUS "MSVC detected, adding compile flags")
  cbor_tags_append_flag(CMAKE_CXX_FLAGS /W4)
  cbor_tags_append_optimization_flag(CMAKE_CXX_FLAGS_DEBUG /Od)
  cbor_tags_append_flag(CMAKE_CXX_FLAGS_DEBUG /Zi)
  cbor_tags_append_optimization_flag(CMAKE_CXX_FLAGS_RELEASE /O2)
endif()

# Add GNU and clang specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(STATUS "GCC detected, adding compile flags")
  cbor_tags_append_flag(CMAKE_CXX_FLAGS -fdiagnostics-color=always -Wno-interference-size)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Clang detected")
endif()
