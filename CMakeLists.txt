cmake_minimum_required(VERSION 3.10)

project(flow_gateway)

add_compile_options(-g -w)

set(BUILD_DIR bin) 

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lwip/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lwip/user
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src ${BUILD_DIR}/Src)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test ${BUILD_DIR}/Test)