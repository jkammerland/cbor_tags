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

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if(CBOR_TAGS_TIDY_TARGET AND CBOR_TAGS_TIDY_BENCHMARKS)
  message(STATUS "Tidy enabled in ${PROJECT_NAME}")
  target_tidy_sources(${PROJECT_NAME})
endif()
