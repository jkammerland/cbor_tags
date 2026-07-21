project(bench_decoder CXX)

# add executables more if needed
add_executable(${PROJECT_NAME})

# add target_sources, all .cpp files in this directory
target_sources(${PROJECT_NAME} PRIVATE bench_decode_preflight.cpp bench_decoder.cpp)

find_package(fmt CONFIG REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE cbor_tags
          fmt::fmt
          doctest
          nanobench
          nameof::nameof
          benchmark_util
          test_util)
cbor_tags_configure_benchmark_target(${PROJECT_NAME})

# add tests for ctest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
