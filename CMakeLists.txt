cmake_minimum_required(VERSION 3.15)
project(motion_detector C)

find_package(JPEG)
find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})

add_executable(motion_detector main.c cam_api.c image_manipulation.c image_manipulation.h quick_select.c quick_select.h)
target_link_libraries(motion_detector ${SDL2_LIBRARIES} ${JPEG_LIBRARIES} m)
