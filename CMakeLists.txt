cmake_minimum_required(VERSION 3.20)
project(tsq CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress hardware_destructive_interference_size warning on GCC
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-interference-size)
endif()

option(ENABLE_TSAN  "ThreadSanitizer"  OFF)
option(ENABLE_ASAN  "AddressSanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g -O1)
    add_link_options   (-fsanitize=thread)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options   (-fsanitize=address,undefined)
endif()

include(CTest)
add_subdirectory(tests)
