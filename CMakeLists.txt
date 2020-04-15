cmake_minimum_required (VERSION 3.7)

project(TightECS)

set(TECS_BUILD_TESTS True CACHE BOOL "Build tests")
set(TECS_BUILD_EXAMPLES True CACHE BOOL "Build examples")

add_library(tecs INTERFACE)
set_property(TARGET tecs PROPERTY INTERFACE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/tecs/tecs.h)
target_include_directories(tecs INTERFACE include)

if(TECS_BUILD_EXAMPLES)
  add_executable(example1 EXCLUDE_FROM_ALL
    example/example1.cpp)
  set_property(TARGET example1 PROPERTY CXX_STANDARD 17)
  target_link_libraries(example1 tecs)
endif()


if(TECS_BUILD_TESTS)
  add_executable(tests EXCLUDE_FROM_ALL
    tests/catch2/catch.hpp
    tests/test_main.cpp
    tests/tests.cpp)
  set_property(TARGET tests PROPERTY CXX_STANDARD 17)
  target_link_libraries(tests tecs)
endif()


add_custom_target(others SOURCES
	LICENSE.txt
	README.md
	CMakeLists.txt)