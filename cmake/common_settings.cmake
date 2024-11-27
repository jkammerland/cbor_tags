message(STATUS "Current cmake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.22)

if(CMAKE_GENERATOR)
  message(STATUS "Using generator [cmake ... -G ${CMAKE_GENERATOR} ...]")
else() # Print active generator
  message(STATUS "No generator set?")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

# Shared flags for all compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG -fconcepts-diagnostics-depth=2") # -fconcepts-diagnostics-depth=2 will not work with tidy
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
  endif()

  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

elseif(MSVC)
  message(STATUS "MSVC detected, adding compile flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2")
endif()

# Add GNU and clang specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(STATUS "GCC detected, adding compile flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -Wno-interference-size")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  message(STATUS "Clang detected")
endif()
