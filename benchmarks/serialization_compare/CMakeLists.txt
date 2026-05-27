project(bench_serialization_compare CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CEREAL_INSTALL OFF CACHE BOOL "" FORCE)
set(glaze_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(glaze_INSTALL OFF CACHE BOOL "" FORCE)
set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)

cpmaddpackage("gh:fraillt/bitsery@5.2.5")
cpmaddpackage("gh:USCiLab/cereal@1.3.2")
cpmaddpackage(NAME zpp_bits GITHUB_REPOSITORY eyalz800/zpp_bits GIT_TAG v4.7.1 DOWNLOAD_ONLY YES)
cpmaddpackage(NAME flatbuffers GITHUB_REPOSITORY google/flatbuffers GIT_TAG v25.12.19)
cpmaddpackage("gh:stephenberry/glaze@7.7.0")

find_package(Boost REQUIRED COMPONENTS serialization)

flatbuffers_generate_headers(
  TARGET bench_compare_flatbuffers_generated
  SCHEMAS ${CMAKE_CURRENT_SOURCE_DIR}/schema/comparison.fbs
  FLAGS --cpp-std c++17)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE bench_serialization_compare.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${zpp_bits_SOURCE_DIR})
target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE CBOR_TAGS_BENCH_BUILD_TYPE="$<CONFIG>"
          CBOR_TAGS_BENCH_BITSERY_VERSION="5.2.5"
          CBOR_TAGS_BENCH_ZPP_BITS_VERSION="4.7.1"
          CBOR_TAGS_BENCH_CEREAL_VERSION="1.3.2"
          CBOR_TAGS_BENCH_FLATBUFFERS_VERSION="25.12.19"
          CBOR_TAGS_BENCH_GLAZE_VERSION="7.7.0")

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE cbor_tags
          nanobench
          benchmark_util
          Bitsery::bitsery
          cereal::cereal
          glaze::glaze
          FlatBuffers::FlatBuffers
          bench_compare_flatbuffers_generated
          Boost::serialization)

cbor_tags_configure_benchmark_target(${PROJECT_NAME})

add_test(
  NAME ${PROJECT_NAME}
  COMMAND ${PROJECT_NAME} --output-dir ${CMAKE_CURRENT_BINARY_DIR}/report)
