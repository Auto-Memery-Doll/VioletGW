# 使用file(GLOB)来获取当前目录下所有的.cpp文件
file(GLOB SOURCES "*.cpp")

# 显示获取的源文件列表
message("Sources: ${SOURCES}")

# 添加可执行文件目标
add_library(cache ${SOURCES})
target_link_libraries(cache base)