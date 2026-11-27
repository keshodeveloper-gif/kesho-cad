
cmake_minimum_required(VERSION 3.22.1)
project(kesho_cad CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(kesho_cad SHARED cad_jni.cpp)

find_library(log-lib log)
target_link_libraries(kesho_cad ${log-lib})
