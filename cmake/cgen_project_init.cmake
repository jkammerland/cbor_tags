# cgen presets init
#
# This file is injected via `CMAKE_PROJECT_<PROJECT_NAME>_INCLUDE` so it runs
# during `project(...)`, before targets are created.

include("${CMAKE_CURRENT_LIST_DIR}/cgen_require_tools.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cgen_sanitizers.cmake")
