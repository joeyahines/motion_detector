cmake_minimum_required(VERSION 3.15)
project(motion_detector C)

find_package(JPEG)
find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})

add_executable(motion_detector main.c cam_api.c image_manipulation.c image_manipulation.h lib/quick_select/quick_select.c lib/quick_select/quick_select.h)
add_executable(motion_detector_test main.c cam_api.c image_manipulation.c image_manipulation.h lib/quick_select/quick_select.c lib/quick_select/quick_select.h lib/libattopng/libattopng.c lib/libattopng/libattopng.h)
target_link_libraries(motion_detector ${SDL2_LIBRARIES} ${JPEG_LIBRARIES} m)
target_link_libraries(motion_detector_test ${SDL2_LIBRARIES} ${JPEG_LIBRARIES} m)

target_compile_definitions(motion_detector_test PUBLIC TEST_MODE)

