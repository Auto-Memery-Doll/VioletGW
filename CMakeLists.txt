cmake_minimum_required(VERSION 3.10)

project(VGW)

include(FetchContent)

# ---------------------------------------------------------------------------
# gflags (datapath CLI) — fetch before global link_libraries
# ---------------------------------------------------------------------------
find_package(gflags QUIET)
if(NOT gflags_FOUND)
    FetchContent_Declare(
        gflags
        GIT_REPOSITORY https://github.com/gflags/gflags.git
        GIT_TAG v2.2.2
        GIT_SHALLOW TRUE
    )
    set(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(GFLAGS_BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
    set(GFLAGS_INSTALL OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(gflags)
endif()

# Generate compile_commands.json for clangd / IDE indexing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-g -w -mssse3)

# ---------------------------------------------------------------------------
# DPDK (project-wide)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)

add_library(vgw_dpdk INTERFACE)
target_link_libraries(vgw_dpdk INTERFACE PkgConfig::DPDK)
target_compile_definitions(vgw_dpdk INTERFACE ALLOW_EXPERIMENTAL_API)
link_libraries(vgw_dpdk)

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

add_library(vgw_spdlog INTERFACE)
target_link_libraries(vgw_spdlog INTERFACE spdlog::spdlog)
link_libraries(vgw_spdlog)

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
