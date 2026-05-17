project(bench_typed_arrays CXX)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE bench_typed_arrays.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE cbor_tags doctest nanobench benchmark_util)

cbor_tags_configure_benchmark_target(${PROJECT_NAME})

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME} "--test-case=rfc8746 typed array endian fixtures roundtrip")
