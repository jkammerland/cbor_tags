message(STATUS "Current cmake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.29)
# 3.29 required for CMAKE_LINKER_TYPE

function(set_output_directories target)
  set_target_properties(
    ${target}
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin_debug"
               LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/lib_debug"
               ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/lib_debug"
               RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin"
               LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/lib"
               ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/lib")
endfunction()

# Uninstall target setup in CMakeLists.txt remains the same, ensuring it references the unified cmake_uninstall.cmake script.
if(NOT TARGET uninstall)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" "# Generated uninstall script\n")
  add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

if(CMAKE_GENERATOR)
  message(STATUS "Using generator [cmake ... -G ${CMAKE_GENERATOR} ...]")
else() # Print active generator
  message(STATUS "No generator set?")
endif()

option(USE_MODULES "Enable C++20 modules" OFF)
if(USE_MODULES MATCHES ON AND CMAKE_GENERATOR MATCHES "Ninja")
  message(STATUS "Enabling C++20 modules")
  set(CMAKE_CXX_SCAN_FOR_MODULES ON)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD ON)
  if(CMAKE_CXX_COMPILER_IMPORT_STD)
    message(STATUS "List CMAKE_CXX_COMPILER_IMPORT_STD contains modules:")
    foreach(item ${CMAKE_CXX_COMPILER_IMPORT_STD})
      message(STATUS "-->${item}")
    endforeach()
  else()
    message(STATUS "No modules found in CMAKE_CXX_COMPILER_IMPORT_STD")
  endif()
else()
  message(STATUS "Disabling C++20 modules, set USE_MODULES=ON and use Ninja generator with Clang or GCC")
  set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message(STATUS "ccache found, using it")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache) # Less useful to do it for linking, see edit2
else()
  message(STATUS "ccache not found - no compiler cache used")
endif(CCACHE_FOUND)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

# Shared flags for all compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

  # Check if mold is available on system
  find_program(MOLD_FOUND mold)
  if(MOLD_FOUND)
    message(STATUS "mold found, using it")
    # set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=mold") set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=mold") set(CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=mold")
    set(CMAKE_LINKER_TYPE "MOLD") # Replacement for the above, used for each add_executable and add_library
  else()
    message(STATUS "mold not found - using default linker")
  endif(MOLD_FOUND)

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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
  message(STATUS "Clang detected")
endif()

# Check active linker
if(CMAKE_LINKER_TYPE)
  message(STATUS "Using linker [${CMAKE_LINKER_TYPE}]")
else()
  message(STATUS "No linker set?")
endif()

include(cmake/print_compiler_and_flags.cmake)
