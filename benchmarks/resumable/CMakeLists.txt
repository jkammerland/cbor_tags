option(CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY "Build resumable comparison harnesses using only the one-shot API" OFF)
option(CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY "Measure named factories owning chunk adapters" OFF)
if(CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY AND CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY)
  message(FATAL_ERROR "Storage factory benchmarks require resumable operations")
endif()
find_package(Threads REQUIRED)

# A private shared runner keeps one-shot timed code independent of resumable
# instantiations and their cold sections. Dispatch and loading happen outside timing.
# Its C++ containers cross the module boundary, so Windows builds need one CRT.
if(MSVC)
  set(_throughput_runtime "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  if(NOT _throughput_runtime)
    set(_throughput_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
  get_target_property(_nanobench_runtime nanobench MSVC_RUNTIME_LIBRARY)
  foreach(_runner_runtime IN ITEMS "${CMAKE_MSVC_RUNTIME_LIBRARY}" "${_nanobench_runtime}")
    if(_runner_runtime AND NOT _runner_runtime MATCHES "-NOTFOUND$" AND
       NOT _runner_runtime STREQUAL "MultiThreadedDLL" AND
       NOT _runner_runtime STREQUAL "MultiThreadedDebugDLL" AND
       NOT _runner_runtime STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
      message(FATAL_ERROR "The private throughput runner requires a shared MSVC runtime for benchmark containers")
    endif()
  endforeach()
  if(_nanobench_runtime AND NOT _nanobench_runtime MATCHES "-NOTFOUND$" AND
     NOT _nanobench_runtime STREQUAL _throughput_runtime)
    message(FATAL_ERROR "The private throughput runner and nanobench must use the same MSVC runtime")
  endif()
endif()

add_library(cbor_throughput_ordinary SHARED throughput_ordinary.cpp throughput_nanobench.cpp)
target_link_libraries(cbor_throughput_ordinary PRIVATE cbor_tags nanobench)
target_compile_definitions(cbor_throughput_ordinary PRIVATE CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1 CBOR_TAGS_ORDINARY_RUNNER_BUILD=1)
set_target_properties(cbor_throughput_ordinary PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
cbor_tags_configure_benchmark_target(cbor_throughput_ordinary)
# Standard-library headers explicitly export some template instantiations even
# with hidden visibility. Keep those helpers local to the measured module too.
if(APPLE)
  set(_ordinary_exports "${CMAKE_CURRENT_BINARY_DIR}/ordinary.exports")
  file(GENERATE OUTPUT "${_ordinary_exports}" CONTENT "__ZN15resumable_bench19make_runner_fixtureE*\n__ZN15resumable_bench17run_ordinary_modeE*\n")
  target_link_options(cbor_throughput_ordinary PRIVATE "LINKER:-exported_symbols_list,${_ordinary_exports}")
elseif(UNIX)
  set(_ordinary_exports "${CMAKE_CURRENT_BINARY_DIR}/ordinary.exports")
  file(GENERATE OUTPUT "${_ordinary_exports}" CONTENT "{ global: _ZN15resumable_bench19make_runner_fixtureE*; _ZN15resumable_bench17run_ordinary_modeE*; local: *; };\n")
  target_link_options(cbor_throughput_ordinary PRIVATE "LINKER:--version-script,${_ordinary_exports}")
endif()
if(DEFINED _ordinary_exports)
  set_property(TARGET cbor_throughput_ordinary APPEND PROPERTY LINK_DEPENDS "${_ordinary_exports}")
endif()
if(MSVC)
  set_target_properties(cbor_throughput_ordinary PROPERTIES MSVC_RUNTIME_LIBRARY "${_throughput_runtime}")
endif()

add_executable(bench_resumable_throughput throughput.cpp)
target_link_libraries(bench_resumable_throughput PRIVATE cbor_tags cbor_throughput_ordinary)
cbor_tags_configure_benchmark_target(bench_resumable_throughput)
if(MSVC)
  set_target_properties(bench_resumable_throughput PROPERTIES MSVC_RUNTIME_LIBRARY "${_throughput_runtime}")
endif()

if(NOT CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY)
  target_sources(bench_resumable_throughput PRIVATE throughput_resumable.cpp)
  target_link_libraries(bench_resumable_throughput PRIVATE nanobench)

  # Both drivers load exactly the same compiled one-shot runner in this build.
  add_executable(bench_resumable_throughput_one_shot throughput.cpp)
  target_compile_definitions(bench_resumable_throughput_one_shot PRIVATE CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1)
  target_link_libraries(bench_resumable_throughput_one_shot PRIVATE cbor_tags cbor_throughput_ordinary)
  cbor_tags_configure_benchmark_target(bench_resumable_throughput_one_shot)
  if(MSVC)
    set_target_properties(bench_resumable_throughput_one_shot PROPERTIES MSVC_RUNTIME_LIBRARY "${_throughput_runtime}")
  endif()
  add_test(NAME resumable_ordinary_throughput_fixtures COMMAND bench_resumable_throughput_one_shot --self-test)
endif()

add_executable(bench_resumable_latency latency.cpp)
target_link_libraries(bench_resumable_latency PRIVATE cbor_tags Threads::Threads)
cbor_tags_configure_benchmark_target(bench_resumable_latency)

if(NOT CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY)
  add_executable(bench_resumable_storage storage.cpp)
  target_link_libraries(bench_resumable_storage PRIVATE cbor_tags nanobench)
  target_include_directories(bench_resumable_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../test_util)
  cbor_tags_configure_benchmark_target(bench_resumable_storage)
  add_test(NAME resumable_storage_fixtures COMMAND bench_resumable_storage --self-test)
endif()

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
