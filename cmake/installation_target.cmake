include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Installation settings
install(
  TARGETS cbor_tags
  EXPORT cbor_tags-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Generate and install export file
install(
  EXPORT cbor_tags-targets
  FILE cbor_tags-targets.cmake
  NAMESPACE cbor_tags::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cbor_tags-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

# Configure the dependencies string for config file
string(JOIN "\n" CBOR_TAGS_PUBLIC_DEPENDENCIES ${CBOR_TAGS_PUBLIC_DEPENDENCIES})

# Generate the config file
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cbor_tags-config.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cbor_tags-config.cmake"
                              INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)

# Install config files
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cbor_tags-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/cbor_tags-config-version.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)
