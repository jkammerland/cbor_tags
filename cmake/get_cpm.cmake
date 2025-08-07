# Allows specifying a custom path to CPM.cmake
if(NOT DEFINED CPM_PATH)
  message(DEBUG "Downloading, if not downloaded already, CPM.cmake to ${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
  message(DEBUG "If you want to use a custom version of CPM, please specify the path in CPM_PATH")
  # If CPM_PATH is not defined, download CPM.cmake
  set(CPM_DOWNLOAD_VERSION 0.42.0)
  set(CPM_HASH_SUM "2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a")
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")

  # Download CPM.cmake
  file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM})
  set(CPM_PATH ${CPM_DOWNLOAD_LOCATION})
endif()

# Include CPM
include(${CPM_PATH})
