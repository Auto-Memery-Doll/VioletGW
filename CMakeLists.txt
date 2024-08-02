cmake_minimum_required(VERSION 3.10)

project(flow_gateway)

add_compile_options(-g -w)

set(BUILD_DIR bin) 

set(SRC ${CMAKE_SOURCE_DIR}/src)
set(LWIP_CORE ${SRC}/lwip)
set(BALANCE_CORE ${SRC}/balance)
set(CACHE_CORE ${SRC}/cache)
set(COROUTINE_CORE ${SRC}/coroutine)
set(THROTTLE_CORE ${SRC}/throttle)
set(BASE_CORE ${SRC}/base)


include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lwip/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lwip/user
)

add_subdirectory(${LWIP_CORE} ${BUILD_DIR}/Lwip)
add_subdirectory(${BASE_CORE} ${BUILD_DIR}/Base)
add_subdirectory(${BALANCE_CORE} ${BUILD_DIR}/Balance)
add_subdirectory(${CACHE_CORE} ${BUILD_DIR}/Cache)
add_subdirectory(${COROUTINE_CORE} ${BUILD_DIR}/Coroutine)
add_subdirectory(${THROTTLE_CORE} ${BUILD_DIR}/Throttle)

add_subdirectory(test ${BUILD_DIR}/Test)