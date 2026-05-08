cmake_minimum_required(VERSION 3.16)
project(TemplateTCPServer LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(TemplateTCPServer
    src/main.cpp
    src/Json.cpp
    src/Protocol.cpp
    src/Server.cpp
    src/EcgGenerator.cpp
)

target_include_directories(TemplateTCPServer PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(TemplateTCPServer PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(TemplateTCPServer PRIVATE ws2_32)
    target_compile_definitions(TemplateTCPServer PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    target_compile_options(TemplateTCPServer PRIVATE /W4 /permissive-)
else()
    target_compile_options(TemplateTCPServer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS TemplateTCPServer RUNTIME DESTINATION bin)
