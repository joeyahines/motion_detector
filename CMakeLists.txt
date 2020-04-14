cmake_minimum_required(VERSION 3.15)
project(motion_detector C)

configure_file(downloadFindFFTW.cmake.in findFFTW-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/findFFTW-download )
if(result)
    message(FATAL_ERROR "CMake step for findFFTW failed: ${result}")
    else()
    message("CMake step for findFFTW completed (${result}).")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/findFFTW-download )
if(result)
    message(FATAL_ERROR "Build step for findFFTW failed: ${result}")
endif()

set(findFFTW_DIR ${CMAKE_CURRENT_BINARY_DIR}/findFFTW-src)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${findFFTW_DIR}")

set(CMAKE_C_STANDARD 99)

find_package(JPEG)
find_package(SDL2 REQUIRED)
find_package(FFTW REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})

add_executable(motion_detector main.c cam_api.c image_manipulation.c image_manipulation.h quick_select.c quick_select.h)
target_link_libraries(motion_detector ${SDL2_LIBRARIES} ${JPEG_LIBRARIES} fftw3 m)
