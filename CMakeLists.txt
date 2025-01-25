cmake_minimum_required(VERSION 3.20)
project(noevent VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COMPILE_EXAMPLES "Compile examples or not" ON)

add_library(noevent)
target_sources(noevent
    PRIVATE src/noevent.cc
    PRIVATE src/epoll.cc
    PRIVATE src/kqueue.cc
    PRIVATE src/select.cc
)
target_include_directories(noevent
    PUBLIC include
)

if(COMPILE_EXAMPLES)
    add_subdirectory(examples)
endif()
