# cgen presets support helpers (tool requirements)
#
# This file is injected via `CMAKE_PROJECT_<PROJECT_NAME>_INCLUDE` so it runs
# during `project(...)`, before targets are created.

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    find_program(CGEN_MOLD_COMMAND NAMES ld.mold mold DOC "Path to mold linker")
    if(NOT CGEN_MOLD_COMMAND)
        message(FATAL_ERROR "cgen presets: mold is required on ${CMAKE_HOST_SYSTEM_NAME} but was not found. Install mold (ensure ld.mold is on PATH).")
    endif()

    foreach(_cgen_linker_flags_var IN ITEMS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
        if(NOT "${${_cgen_linker_flags_var}}" MATCHES "(^|[ \t])-fuse-ld=")
            if("${${_cgen_linker_flags_var}}" STREQUAL "")
                set(${_cgen_linker_flags_var} "-fuse-ld=mold")
            else()
                set(${_cgen_linker_flags_var} "${${_cgen_linker_flags_var}} -fuse-ld=mold")
            endif()
            set(${_cgen_linker_flags_var} "${${_cgen_linker_flags_var}}" CACHE STRING "Linker flags" FORCE)
        endif()
    endforeach()
    unset(_cgen_linker_flags_var)
endif()

# Valgrind (MemCheck) is only required when requested by presets.
if(DEFINED CGEN_REQUIRE_VALGRIND AND CGEN_REQUIRE_VALGRIND)
    find_program(CGEN_VALGRIND_COMMAND NAMES valgrind DOC "Path to valgrind")
    if(NOT CGEN_VALGRIND_COMMAND)
        message(FATAL_ERROR "cgen presets: valgrind preset requested but valgrind was not found. Install valgrind.")
    endif()

    set(MEMORYCHECK_COMMAND "${CGEN_VALGRIND_COMMAND}" CACHE FILEPATH "Path to the memory checking command, used for memory error detection." FORCE)
endif()
