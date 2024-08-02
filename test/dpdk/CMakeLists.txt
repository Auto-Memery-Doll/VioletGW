cmake_minimum_required(VERSION 3.10)

# 项目名称
project(stack)

add_compile_options(-w -g)

# 添加源文件
set(SOURCES 
    main.c)

# 查找pkg-config
find_program(PKGCONFIG_EXECUTABLE pkg-config REQUIRED)

# 使用pkg-config检查DPDK是否存在
execute_process(COMMAND ${PKGCONFIG_EXECUTABLE} --exists libdpdk
                RESULT_VARIABLE DPDK_FOUND_CODE)
if(NOT DPDK_FOUND_CODE EQUAL 0)
    message(FATAL_ERROR "no installation of DPDK found")
endif()

# 获取DPDK的编译和链接参数
execute_process(COMMAND ${PKGCONFIG_EXECUTABLE} --cflags libdpdk
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE DPDK_CFLAGS_CODE
                OUTPUT_VARIABLE DPDK_CFLAGS)
if(NOT DPDK_CFLAGS_CODE EQUAL 0)
    message(FATAL_ERROR "Failed to get DPDK CFLAGS")
endif()

execute_process(COMMAND ${PKGCONFIG_EXECUTABLE} --libs libdpdk
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE DPDK_LDFLAGS_CODE
                OUTPUT_VARIABLE DPDK_LDFLAGS_SHARED)
if(NOT DPDK_LDFLAGS_CODE EQUAL 0)
    message(FATAL_ERROR "Failed to get DPDK LDFLAGS (shared)")
endif()

execute_process(COMMAND ${PKGCONFIG_EXECUTABLE} --static --libs libdpdk
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE DPDK_LDFLAGS_STATIC_CODE
                OUTPUT_VARIABLE DPDK_LDFLAGS_STATIC)
if(NOT DPDK_LDFLAGS_STATIC_CODE EQUAL 0)
    message(FATAL_ERROR "Failed to get DPDK LDFLAGS (static)")
endif()

# 验证静态链接配置是否包含"whole-archive"
string(FIND "${DPDK_LDFLAGS_STATIC}" "-Wl,--whole-archive" WHOLE_ARCHIVE_POS)
if(WHOLE_ARCHIVE_POS EQUAL -1)
    message(FATAL_ERROR "Cannot generate statically-linked binaries with this version of pkg-config")
endif()

# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 ${DPDK_CFLAGS} -DALLOW_EXPERIMENTAL_API")

# 输出目录设置
set(BUILD_DIR build)
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD_DIR})

# 添加可执行文件目标
add_executable(${PROJECT_NAME}-shared ${SOURCES})
target_link_libraries(${PROJECT_NAME}-shared PRIVATE ${DPDK_LDFLAGS_SHARED})
add_executable(${PROJECT_NAME}-static ${SOURCES})
target_link_libraries(${PROJECT_NAME}-static PRIVATE ${DPDK_LDFLAGS_STATIC})

# 创建软链接
add_custom_command(TARGET ${PROJECT_NAME}-shared POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${PROJECT_NAME}-shared> ${BUILD_DIR}/${PROJECT_NAME})
add_custom_command(TARGET ${PROJECT_NAME}-static POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${PROJECT_NAME}-static> ${BUILD_DIR}/${PROJECT_NAME})

# 清理规则
add_custom_target(clear
                  COMMAND ${CMAKE_COMMAND} -E remove ${BUILD_DIR}/${PROJECT_NAME}* 
                  COMMAND ${CMAKE_COMMAND} -E remove_directory ${BUILD_DIR}
                  VERBATIM)