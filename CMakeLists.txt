cmake_minimum_required(VERSION 3.20)
project(StellarisNationPlanetDashboardParser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_executable(stellaris_parser
    parser-program-files/src/ast.cpp
    parser-program-files/src/cli.cpp
    parser-program-files/src/config.cpp
    parser-program-files/src/country_colony_export.cpp
    parser-program-files/src/country_demographics_export.cpp
    parser-program-files/src/country_export.cpp
    parser-program-files/src/country_export_helpers.cpp
    parser-program-files/src/country_finance_export.cpp
    parser-program-files/src/country_leader_export.cpp
    parser-program-files/src/country_map_export.cpp
    parser-program-files/src/country_market_export.cpp
    parser-program-files/src/country_military_export.cpp
    parser-program-files/src/game_indexes.cpp
    parser-program-files/src/json_writer.cpp
    parser-program-files/src/main.cpp
    parser-program-files/src/manifest.cpp
    parser-program-files/src/pdx_parser.cpp
    parser-program-files/src/performance.cpp
    parser-program-files/src/save_reader.cpp
    parser-program-files/src/self_test.cpp
    parser-program-files/src/timeline_export.cpp
    parser-program-files/src/utils.cpp
)

target_include_directories(stellaris_parser PRIVATE
    parser-program-files/include
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
