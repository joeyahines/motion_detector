cmake_minimum_required(VERSION 3.15)
project(motion_detector C)

set(CMAKE_C_STANDARD 99)

find_package(JPEG)
find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})

add_executable(motion_detector main.c cam_api.c image_manipulation.c image_manipulation.h)
target_link_libraries(motion_detector ${SDL2_LIBRARIES} ${JPEG_LIBRARIES} m)
