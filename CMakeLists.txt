cmake_minimum_required(VERSION 3.0.0)

project(switchbuffer CXX)

# header-only library
add_library(switchbuffer INTERFACE)
target_include_directories(switchbuffer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
export(TARGETS switchbuffer FILE switchbuffer-config.cmake)

add_executable(switchbuffer_test switchbuffer_test.cpp)
target_link_libraries(switchbuffer_test switchbuffer)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  target_link_libraries(switchbuffer_test pthread)
endif()
