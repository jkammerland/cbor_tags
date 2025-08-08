project(bench_encoder CXX)

# add executables more if needed
add_executable(${PROJECT_NAME})

# add target_sources, all .cpp files in this directory
target_sources(${PROJECT_NAME} PRIVATE bench_encoder.cpp)

find_package(fmt CONFIG REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE cbor_tags
          fmt::fmt
          doctest
          nanobench
          nameof
          benchmark_util
          test_util)

# add tests for ctest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if(CBOR_TAGS_TIDY_TARGET)
  message(STATUS "Tidy enabled in ${PROJECT_NAME}")
  target_tidy_sources(${PROJECT_NAME})
endif()
