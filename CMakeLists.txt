cmake_minimum_required(VERSION 3.15)
project(motion_detector C)

set(CMAKE_C_STANDARD 99)

add_executable(motion_detector main.c)

find_package(JPEG)
target_link_libraries(motion_detector ${JPEG_LIBRARIES})

