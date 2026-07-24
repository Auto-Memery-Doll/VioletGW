cmake_minimum_required(VERSION 3.10)

project(flow_gateway)

include(FetchContent)

# Generate compile_commands.json for clangd / IDE indexing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-g -w -mssse3)

# ---------------------------------------------------------------------------
# DPDK (project-wide)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)

add_library(fg_dpdk INTERFACE)
target_link_libraries(fg_dpdk INTERFACE PkgConfig::DPDK)
target_compile_definitions(fg_dpdk INTERFACE ALLOW_EXPERIMENTAL_API)
link_libraries(fg_dpdk)

# ---------------------------------------------------------------------------
# spdlog (control-plane logging)
# ---------------------------------------------------------------------------
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        GIT_SHALLOW TRUE
    )
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

add_library(fg_spdlog INTERFACE)
target_link_libraries(fg_spdlog INTERFACE spdlog::spdlog)
link_libraries(fg_spdlog)

enable_testing()

set(BUILD_DIR bin)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src ${BUILD_DIR}/Src)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test ${BUILD_DIR}/Test)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )
endif()
