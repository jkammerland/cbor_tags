cmake_minimum_required(VERSION 3.20)
project(tests VERSION 1.0.0)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.1.2")
cpmaddpackage("gh:Neargye/nameof@0.10.4")
cpmaddpackage("gh:Neargye/magic_enum@0.9.7")
cpmaddpackage("gh:ericniebler/range-v3#ca1388fb9da8e69314dda222dc7b139ca84e092f")
cpmaddpackage("gh:jkammerland/doctest@1.0.1")

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER TEST_SOURCES EXCLUDE REGEX "/vsomeip/")
target_sources(tests PRIVATE ${TEST_SOURCES})

# add test include directories
target_link_libraries(tests PUBLIC test_util)

# add link libraries
target_link_libraries(tests PRIVATE cbor::tags doctest::doctest fmt::fmt nameof::nameof magic_enum::magic_enum range-v3::range-v3)

# Enable ASAN for linux and macos
option(CBOR_TAGS_ENABLE_ASAN "Enable ASAN in tests, DEFAULT=OFF" OFF)
if(CBOR_TAGS_ENABLE_ASAN)
  message(STATUS "ASAN enabled in tests")
  if((UNIX OR APPLE) AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    target_compile_options(tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tests PRIVATE -fsanitize=address)
  elseif(WIN32)
    # target_compile_options(tests PRIVATE /fsanitize=address) target_link_options(tests PRIVATE /fsanitize=address)
  endif()
endif()

if(CBOR_TAGS_TIDY_TARGET)
  message(STATUS "Tidy enabled in tests")
  target_tidy_sources(tests)
endif()

# add tests for ctest
add_test(NAME tests COMMAND tests)

# vSomeIP E2E tests (optional)
option(CBOR_TAGS_ENABLE_VSOMEIP_TESTS "Build vSomeIP E2E tests" ON)
if (CBOR_TAGS_ENABLE_VSOMEIP_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vsomeip/CMakeLists.txt")
  set(_vsomeip_exe_flags "${CMAKE_EXE_LINKER_FLAGS}")
  set(_vsomeip_shared_flags "${CMAKE_SHARED_LINKER_FLAGS}")
  set(_vsomeip_module_flags "${CMAKE_MODULE_LINKER_FLAGS}")
  foreach(_vsomeip_flag_var IN ITEMS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
    if("${${_vsomeip_flag_var}}" MATCHES "-fuse-ld=mold")
      string(REPLACE "-fuse-ld=mold" "-fuse-ld=bfd" ${_vsomeip_flag_var} "${${_vsomeip_flag_var}}")
    endif()
  endforeach()
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vsomeip ${CMAKE_BINARY_DIR}/vsomeip EXCLUDE_FROM_ALL)
  set(CMAKE_EXE_LINKER_FLAGS "${_vsomeip_exe_flags}")
  set(CMAKE_SHARED_LINKER_FLAGS "${_vsomeip_shared_flags}")
  set(CMAKE_MODULE_LINKER_FLAGS "${_vsomeip_module_flags}")
  unset(_vsomeip_exe_flags)
  unset(_vsomeip_shared_flags)
  unset(_vsomeip_module_flags)
  unset(_vsomeip_flag_var)
  add_executable(vsomeip_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/vsomeip/vsomeip_e2e.cpp
  )
  target_compile_definitions(vsomeip_tests PRIVATE VSOMEIP_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vsomeip/conf")
  target_link_libraries(vsomeip_tests PRIVATE cbor::tags vsomeip3 vsomeip3-sd vsomeip3-cfg pthread)
  set_target_properties(vsomeip_tests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/vsomeip")
  add_test(NAME vsomeip_tests_tcp COMMAND vsomeip_tests tcp)
  add_test(NAME vsomeip_tests_udp COMMAND vsomeip_tests udp)
  if(UNIX AND NOT APPLE)
    set_tests_properties(vsomeip_tests_tcp vsomeip_tests_udp PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/vsomeip:$ENV{LD_LIBRARY_PATH}")
  elseif(APPLE)
    set_tests_properties(vsomeip_tests_tcp vsomeip_tests_udp PROPERTIES
      ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/vsomeip:$ENV{DYLD_LIBRARY_PATH}")
  endif()
endif()
