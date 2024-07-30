cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(lwip)

# 设置 include_directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# 将 include 子目录添加到每个源文件的 include_path
foreach(SOURCE_FILE ${SOURCES})
    get_filename_component(SOURCE_DIR ${SOURCE_FILE} DIRECTORY)
    if(${SOURCE_DIR} STREQUAL "api" OR ${SOURCE_DIR} STREQUAL "compat" OR ${SOURCE_DIR} STREQUAL "core")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES
            COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/include/lwip")
    endif()
endforeach()

# 遍历当前目录下的所有文件夹
file(GLOB_RECURSE SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 输出找到的源文件列表
message(STATUS "Found sources: ${SOURCES}")

# 创建静态库
add_library(lwip STATIC ${SOURCES})

# 设置输出路径
set_target_properties(lwip PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 设置编译选项
target_compile_options(lwip PRIVATE -Wall -Wextra)