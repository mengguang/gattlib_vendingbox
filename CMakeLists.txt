cmake_minimum_required(VERSION 3.7)
project(20191207_gattlib_vendingbox)

set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GATTLIB REQUIRED gattlib)

add_executable(20191207_gattlib_vendingbox main.cpp)
target_link_libraries(20191207_gattlib_vendingbox ${GATTLIB_LIBRARIES})
target_include_directories(20191207_gattlib_vendingbox PUBLIC ${GATTLIB_INCLUDE_DIRS})
target_compile_options(20191207_gattlib_vendingbox PUBLIC ${GATTLIB_CFLAGS_OTHER})