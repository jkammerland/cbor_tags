# Allows specifying a custom path to CPM.cmake
if(NOT DEFINED CPM_PATH)
  message(DEBUG "Downloading, if not downloaded already, CPM.cmake to ${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
  message(DEBUG "If you want to use a custom version of CPM, please specify the path in CPM_PATH")
  # If CPM_PATH is not defined, download CPM.cmake
  set(CPM_DOWNLOAD_VERSION 0.40.5)
  set(CPM_HASH_SUM "c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d")
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
  
  # Download CPM.cmake
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
    EXPECTED_HASH SHA256=${CPM_HASH_SUM}
  )
  set(CPM_PATH ${CPM_DOWNLOAD_LOCATION})
endif()

# Include CPM
include(${CPM_PATH})