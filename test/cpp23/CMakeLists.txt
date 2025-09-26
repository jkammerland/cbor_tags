cmake_minimum_required(VERSION 3.20)

add_executable(tests-cpp23)
target_compile_features(tests-cpp23 PUBLIC cxx_std_23)
target_sources(tests-cpp23 PRIVATE test_cbor_iterator.cpp ../main.cpp)
target_link_libraries(
  tests-cpp23
  PRIVATE cbor::tags
          test_util
          doctest::doctest
          fmt::fmt
          nameof::nameof
          magic_enum::magic_enum
          range-v3::range-v3)

if(CBOR_TAGS_ENABLE_ASAN)
  if((UNIX OR APPLE) AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    target_compile_options(tests-cpp23 PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tests-cpp23 PRIVATE -fsanitize=address)
  endif()
endif()

add_test(NAME tests-cpp23 COMMAND tests-cpp23)
