cmake_minimum_required (VERSION 3.0.0)

project (switchbuffer CXX)
add_executable (switchbuffer_test switchbuffer_test.cpp switchbuffer.h switchbuffer_impl.h)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  target_link_libraries (switchbuffer_test pthread)
endif()
