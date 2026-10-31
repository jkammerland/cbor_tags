option(CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY "Build resumable comparison harnesses using only the ordinary API" OFF)
option(CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY "Measure named factories owning chunk adapters" OFF)
if(CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY AND CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY)
  message(FATAL_ERROR "Storage factory benchmarks require resumable operations")
endif()
find_package(Threads REQUIRED)

add_executable(bench_resumable_throughput throughput.cpp)
target_link_libraries(bench_resumable_throughput PRIVATE cbor_tags nanobench)
cbor_tags_configure_benchmark_target(bench_resumable_throughput)

add_executable(bench_resumable_latency latency.cpp)
target_link_libraries(bench_resumable_latency PRIVATE cbor_tags Threads::Threads)
cbor_tags_configure_benchmark_target(bench_resumable_latency)

if(CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY)
  target_compile_definitions(bench_resumable_throughput PRIVATE CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1)
  target_compile_definitions(bench_resumable_latency PRIVATE CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1)
endif()

if(CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY)
  target_compile_definitions(bench_resumable_throughput PRIVATE CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY=1)
  target_compile_definitions(bench_resumable_latency PRIVATE CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY=1)
endif()

add_test(NAME resumable_throughput_fixtures COMMAND bench_resumable_throughput --self-test)
add_test(NAME resumable_latency_self_test COMMAND bench_resumable_latency --self-test)

add_test(NAME resumable_allocation_fixtures COMMAND bench_resumable_throughput --allocations --bytes 16384)
