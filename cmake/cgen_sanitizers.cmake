# cgen presets support helpers (sanitizers)
#
# This file is injected via `CMAKE_PROJECT_<PROJECT_NAME>_INCLUDE` so it runs
# during `project(...)`, before targets are created.

if(NOT DEFINED CGEN_SANITIZERS OR CGEN_SANITIZERS STREQUAL "")
    message(TRACE "cgen_sanitizers: CGEN_SANITIZERS not set; skipping")
    return()
endif()

message(TRACE "cgen_sanitizers: CGEN_SANITIZERS='${CGEN_SANITIZERS}'")

set(_cgen_sanitizers "${CGEN_SANITIZERS}")
string(REPLACE "," ";" _cgen_sanitizers "${_cgen_sanitizers}")
message(TRACE "cgen_sanitizers: parsed list='${_cgen_sanitizers}'")

set(_cgen_config "DEBUG")
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _cgen_config)
endif()
message(TRACE "cgen_sanitizers: CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' config='${_cgen_config}'")
message(TRACE "cgen_sanitizers: CMAKE_CXX_COMPILER_ID='${CMAKE_CXX_COMPILER_ID}' frontend='${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}' MSVC='${MSVC}'")

set(_cgen_msvc_like FALSE)
if(MSVC)
    set(_cgen_msvc_like TRUE)
elseif(DEFINED CMAKE_CXX_COMPILER_FRONTEND_VARIANT AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_cgen_msvc_like TRUE)
endif()

if(_cgen_msvc_like)
    list(FIND _cgen_sanitizers "address" _cgen_address_idx)
    if(_cgen_address_idx EQUAL -1)
        message(STATUS "cgen: sanitizer preset requested (${CGEN_SANITIZERS}) but MSVC/clang-cl only supports AddressSanitizer")
        return()
    endif()

    message(TRACE "cgen_sanitizers: enabling MSVC ASan (/fsanitize=address); other sanitizers (if any) are ignored")

    string(APPEND CMAKE_C_FLAGS_${_cgen_config} " /fsanitize=address")
    string(APPEND CMAKE_CXX_FLAGS_${_cgen_config} " /fsanitize=address")

    string(APPEND CMAKE_EXE_LINKER_FLAGS_${_cgen_config} " /fsanitize=address /INCREMENTAL:NO")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_${_cgen_config} " /fsanitize=address /INCREMENTAL:NO")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_${_cgen_config} " /fsanitize=address /INCREMENTAL:NO")

    message(TRACE "cgen_sanitizers: CMAKE_C_FLAGS_${_cgen_config}='${CMAKE_C_FLAGS_${_cgen_config}}'")
    message(TRACE "cgen_sanitizers: CMAKE_CXX_FLAGS_${_cgen_config}='${CMAKE_CXX_FLAGS_${_cgen_config}}'")
    message(TRACE "cgen_sanitizers: CMAKE_EXE_LINKER_FLAGS_${_cgen_config}='${CMAKE_EXE_LINKER_FLAGS_${_cgen_config}}'")
else()
    string(JOIN "," _cgen_sanitize_csv ${_cgen_sanitizers})
    set(_cgen_sanitize_flag "-fsanitize=${_cgen_sanitize_csv}")
    message(TRACE "cgen_sanitizers: enabling '${_cgen_sanitize_flag}'")

    string(APPEND CMAKE_C_FLAGS_${_cgen_config} " -O1 -fno-omit-frame-pointer ${_cgen_sanitize_flag}")
    string(APPEND CMAKE_CXX_FLAGS_${_cgen_config} " -O1 -fno-omit-frame-pointer ${_cgen_sanitize_flag}")

    string(APPEND CMAKE_EXE_LINKER_FLAGS_${_cgen_config} " ${_cgen_sanitize_flag}")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_${_cgen_config} " ${_cgen_sanitize_flag}")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_${_cgen_config} " ${_cgen_sanitize_flag}")

    message(TRACE "cgen_sanitizers: CMAKE_C_FLAGS_${_cgen_config}='${CMAKE_C_FLAGS_${_cgen_config}}'")
    message(TRACE "cgen_sanitizers: CMAKE_CXX_FLAGS_${_cgen_config}='${CMAKE_CXX_FLAGS_${_cgen_config}}'")
    message(TRACE "cgen_sanitizers: CMAKE_EXE_LINKER_FLAGS_${_cgen_config}='${CMAKE_EXE_LINKER_FLAGS_${_cgen_config}}'")
endif()
