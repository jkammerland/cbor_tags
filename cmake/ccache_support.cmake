cmake_minimum_required(VERSION 3.22)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message(STATUS "ccache found, using it")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache) # Less useful to do it for linking, see edit2
else()
  message(STATUS "ccache not found - no compiler cache used")
endif(CCACHE_FOUND)
