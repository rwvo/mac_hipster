cmake_minimum_required(VERSION 3.10)
project(HipHello LANGUAGES CXX HIP)

find_package(HIP REQUIRED)

add_executable(hip_hello hip_hello.cpp)
set_source_files_properties(hip_hello.cpp PROPERTIES LANGUAGE HIP)
