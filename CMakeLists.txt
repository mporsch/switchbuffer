cmake_minimum_required(VERSION 3.0.0)

project(switchbuffer CXX)

option(SWITCHBUFFER_BUILD_TESTS "Build test executable(s)" FALSE)

# header-only library
add_library(switchbuffer INTERFACE)
target_include_directories(switchbuffer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
export(TARGETS switchbuffer FILE switchbuffer-config.cmake)

if(SWITCHBUFFER_BUILD_TESTS)
  add_executable(switchbuffer_test switchbuffer_test.cpp)
  target_link_libraries(switchbuffer_test switchbuffer $<$<PLATFORM_ID:Linux>:pthread>)
endif()
