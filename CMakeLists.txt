cmake_minimum_required(VERSION 3.10)

project(switchbuffer CXX)

if(${CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT})
  # install to the build directory by default
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "" FORCE)
endif()

option(SWITCHBUFFER_BUILD_TESTS "Build test executable(s)" FALSE)

# header-only library
set(SWITCHBUFFER_PUBLIC_HEADER
  switchbuffer.h
  switchbuffer_impl.h
)
add_library(switchbuffer INTERFACE ${SWITCHBUFFER_PUBLIC_HEADER})
target_include_directories(switchbuffer INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(switchbuffer PROPERTIES
  PUBLIC_HEADER "${SWITCHBUFFER_PUBLIC_HEADER}"
)

export(TARGETS switchbuffer FILE switchbuffer-config.cmake)
install(EXPORT switchbuffer-config DESTINATION lib/cmake/switchbuffer)
install(TARGETS switchbuffer
  EXPORT switchbuffer-config
  PUBLIC_HEADER DESTINATION include
)

if(SWITCHBUFFER_BUILD_TESTS)
  add_executable(switchbuffer_test switchbuffer_test.cpp)
  target_link_libraries(switchbuffer_test switchbuffer $<$<PLATFORM_ID:Linux>:pthread>)
endif()
