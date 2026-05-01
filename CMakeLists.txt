cmake_minimum_required(VERSION 3.20)
project(StellarisNationPlanetDashboardParser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_executable(stellaris_parser
    parser-program-files/src/main.cpp
)

target_link_libraries(stellaris_parser PRIVATE ZLIB::ZLIB)

target_compile_definitions(stellaris_parser PRIVATE
    STELLARIS_PARSER_VERSION="0.1.0"
)

if (MSVC)
    target_compile_options(stellaris_parser PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(stellaris_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()
