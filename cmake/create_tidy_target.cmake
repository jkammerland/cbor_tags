function(create_tidy_target target_dir)
  message(STATUS "Creating tidy target for directory: ${target_dir}")

  # Recursively gather all source files from the given directory
  file(GLOB_RECURSE ALL_SOURCE_FILES ${target_dir}/*.cpp ${target_dir}/*.c ${target_dir}/*.h ${target_dir}/*.hpp)

  # Check if any source files were found
  if(NOT ALL_SOURCE_FILES)
    message(WARNING "No source files found in directory: ${target_dir}")
    return()
  endif()

  # Check if STL_INCLUDE_PATH is not already set in the cache
  if(NOT DEFINED STL_INCLUDE_PATH)
    if(UNIX AND NOT APPLE)
      set(STL_DEFAULT_PATH "/usr/include/c++/14")
    elseif(APPLE)
      # For macOS, adapt the path as necessary, this is just a generic example set(STL_DEFAULT_PATH "/Applications/Xcode.app/path-to-STL-headers")
    elseif(WIN32)
      # This might need further adjustment depending on the setup set(STL_DEFAULT_PATH "C:/path/to/windows/STL/headers")
    endif()

    # Set STL_INCLUDE_PATH in the cache, defaulting to STL_DEFAULT_PATH if it's set
    if(DEFINED STL_DEFAULT_PATH)
      set(STL_INCLUDE_PATH
          "${STL_DEFAULT_PATH}"
          CACHE PATH "Path to the STL headers" FORCE)
    endif()
  endif()

  # Check if the STL headers exist
  if(NOT EXISTS ${STL_INCLUDE_PATH})
    message(FATAL_ERROR "STL_INCLUDE_PATH does not exist: ${STL_INCLUDE_PATH}")
  endif()

  message(STATUS "Using STL headers from: ${STL_INCLUDE_PATH}")

  # Name of the output file for clang-tidy results
  set(TIDY_OUTPUT_FILE "${CMAKE_BINARY_DIR}/clang-tidy-output.txt")

  # Create custom tidy target
  add_custom_target(
    tidy
    COMMAND
      clang-tidy -p ${CMAKE_BINARY_DIR} --extra-arg=-I${STL_INCLUDE_PATH} ${ALL_SOURCE_FILES}
      # Only add the --use-color option if on a Unix-like OS:
      $<$<OR:$<PLATFORM_ID:Linux>,$<PLATFORM_ID:Darwin>>:--use-color>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Ensure compile_commands.json is generated for Clang-Tidy
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endfunction()
