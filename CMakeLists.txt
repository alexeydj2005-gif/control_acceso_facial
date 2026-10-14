cmake_minimum_required(VERSION 3.16)
project(control_acceso CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)

add_executable(control_acceso src/main.cpp)
target_link_libraries(control_acceso PRIVATE ${OpenCV_LIBS} mysqlcppconn)
