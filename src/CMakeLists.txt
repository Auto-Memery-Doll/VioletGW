set(SRC ${CMAKE_SOURCE_DIR}/src)
set(LWIP_CORE ${SRC}/lwip)
set(BALANCE_CORE ${SRC}/balance)
set(CACHE_CORE ${SRC}/cache)
set(COROUTINE_CORE ${SRC}/coroutine)
set(THROTTLE_CORE ${SRC}/throttle)
set(BASE_CORE ${SRC}/base)

add_subdirectory(${LWIP_CORE} ${BUILD_DIR}/Lwip)
add_subdirectory(${BASE_CORE} ${BUILD_DIR}/Base)
add_subdirectory(${BALANCE_CORE} ${BUILD_DIR}/Balance)
add_subdirectory(${CACHE_CORE} ${BUILD_DIR}/Cache)
add_subdirectory(${COROUTINE_CORE} ${BUILD_DIR}/Coroutine)
add_subdirectory(${THROTTLE_CORE} ${BUILD_DIR}/Throttle)

# 使用file(GLOB)来获取当前目录下所有的.cpp文件
file(GLOB SOURCES "*.cpp")

# 显示获取的源文件列表
message("Sources: ${SOURCES}")

# 添加可执行文件目标
add_executable(flow_gw ${SOURCES})