include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/user
)

# 遍历当前目录下的所有文件夹
file(GLOB_RECURSE SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 输出找到的源文件列表
message(STATUS "Found sources: ${SOURCES}")

# 创建静态库
add_library(lwip ${SOURCES})