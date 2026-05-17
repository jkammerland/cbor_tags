project(bench_ranges CXX)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE bench_ranges.cpp)

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
cbor_tags_configure_benchmark_target(${PROJECT_NAME})

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME} "--test-case=String payload throughput fixtures roundtrip")
