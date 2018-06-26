cmake_minimum_required (VERSION 3.0.0)

project (switchbuffer CXX)
add_executable (switchbuffer_test switchbuffer_test.cpp switchbuffer.h switchbuffer_impl.h)
target_link_libraries (switchbuffer_test pthread)
