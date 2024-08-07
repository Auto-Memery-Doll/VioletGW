add_executable(helloword main.c)
target_link_libraries(helloword base)

add_executable(dma dma.cpp)
target_link_libraries(dma base)