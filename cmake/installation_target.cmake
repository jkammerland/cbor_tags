include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(create_installation_target TARGET_NAME)
  if(NOT TARGET ${TARGET_NAME})
    message(FATAL_ERROR "Target '${TARGET_NAME}' does not exist.")
  endif()

  # Get the source directory for this target
  get_target_property(TARGET_SOURCE_DIR ${TARGET_NAME} SOURCE_DIR)
  
  install(
    TARGETS ${TARGET_NAME}
    EXPORT ${TARGET_NAME}-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME})

  # Install headers to a target-specific include directory
  install(DIRECTORY ${TARGET_SOURCE_DIR}/include/ 
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME})

  # Check if project has config file
  if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/include/${TARGET_NAME}/${TARGET_NAME}_config.h)
    message(DEBUG "Found config file for target: ${TARGET_NAME}")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/${TARGET_NAME}/${TARGET_NAME}_config.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TARGET_NAME})
  else()
    message(DEBUG "No config file found for target: ${TARGET_NAME}")
  endif()

  install(
    EXPORT ${TARGET_NAME}-targets
    FILE ${TARGET_NAME}-targets.cmake
    NAMESPACE ${TARGET_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  string(JOIN "\n" ${TARGET_NAME}_PUBLIC_DEPENDENCIES ${${TARGET_NAME}_PUBLIC_DEPENDENCIES})

  # Look for config template in target's directory first, then project directory
  if(EXISTS "${TARGET_SOURCE_DIR}/cmake/${TARGET_NAME}-config.cmake.in")
    set(CONFIG_TEMPLATE "${TARGET_SOURCE_DIR}/cmake/${TARGET_NAME}-config.cmake.in")
  else()
    set(CONFIG_TEMPLATE "${PROJECT_SOURCE_DIR}/cmake/${TARGET_NAME}-config.cmake.in")
  endif()

  configure_package_config_file(
    "${CONFIG_TEMPLATE}"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})

  install(
    FILES 
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME})
endfunction()