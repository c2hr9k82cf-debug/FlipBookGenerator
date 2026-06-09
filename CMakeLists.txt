cmake_minimum_required(VERSION 3.14)
project(FlipBookGenerator VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(flipbook src/main.cpp)
target_link_libraries(flipbook ${OpenCV_LIBS})
