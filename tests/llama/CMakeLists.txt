cmake_minimum_required(VERSION 3.20)

project(llama_smoke_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(llama CONFIG REQUIRED)

add_executable(test-llama
    test-llama.cpp
)

target_link_libraries(test-llama PRIVATE llama)