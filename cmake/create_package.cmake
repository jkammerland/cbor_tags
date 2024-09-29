# This function creates a package for a given target in CMake.
#
# Parameters: - target_name: The name of the target for which the package is being created. - NAMESPACE: The namespace for the exported targets. - VERSION: The version of the package.
#
# Usage Example: create_package(mylib NAMESPACE "mylib" VERSION
# "${PROJECT_VERSION}")
#
# Preconditions: - The target must exist and be properly configured with the PUBLIC_HEADER property. - A template file for the Config file (targetNameConfig.cmake.in) must exist in the project's cmake
# directory. - An uninstall script template (cmake_uninstall.cmake.in) must be provided if uninstall functionality is desired.
function(create_package target_name)
  set(options "")
  set(oneValueArgs NAMESPACE VERSION)
  set(multiValueArgs "")
  cmake_parse_arguments(PKG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "Specified target '${target_name}' does not exist.")
  endif()

  get_target_property(target_exists ${target_name} NAME)
  if(NOT target_exists)
    message(FATAL_ERROR "Target ${target_name} must be defined before calling create_package.")
  endif()

  # Assuming target includes PUBLIC_HEADER in set_target_properties
  get_target_property(target_public_header ${target_name} PUBLIC_HEADER)
  if(NOT target_public_header)
    message(FATAL_ERROR "PUBLIC_HEADER property not set for target ${target_name}. Package creation aborted.")
  endif()

  message(STATUS "Creating package for target ${target_name}, version ${PKG_VERSION}")
  install(
    TARGETS ${target_name}
    EXPORT ${target_name}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${target_name}
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(
    EXPORT ${target_name}Targets
    FILE ${target_name}Targets.cmake
    NAMESPACE ${PKG_NAMESPACE}::
    DESTINATION lib/cmake/${target_name})

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    "${target_name}ConfigVersion.cmake"
    VERSION ${PKG_VERSION}
    COMPATIBILITY SameMajorVersion)

  set(config_file_in "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${target_name}Config.cmake.in")
  if(NOT EXISTS ${config_file_in})
    message(FATAL_ERROR "Config template file '${config_file_in}' does not exist. Package creation aborted.")
  endif()

  configure_file("${config_file_in}" "${CMAKE_CURRENT_BINARY_DIR}/${target_name}Config.cmake" @ONLY)

  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}Config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/${target_name}ConfigVersion.cmake" DESTINATION lib/cmake/${target_name})

  set(uninstall_script "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in" uninstall_commands)
    # Replace template variables in uninstall_commands if needed
    string(REPLACE "<TARGET_NAME>" "${target_name}" uninstall_commands "${uninstall_commands}")
    file(APPEND ${uninstall_script} "${uninstall_commands}\n")
  endif()

  message(STATUS "Package for target ${target_name} created successfully.")
endfunction()
