set(SRC ${CMAKE_SOURCE_DIR}/src)

add_subdirectory(${SRC}/base ${BUILD_DIR}/Base)
add_subdirectory(${SRC}/dpdk ${BUILD_DIR}/Dpdk)

add_library(packet packet.cpp)
target_include_directories(packet PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(packet PUBLIC neighbor)

add_library(session session.cpp)
target_include_directories(session PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(neighbor neighbor.cpp)
target_include_directories(neighbor PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(arp arp.cpp)
target_include_directories(arp PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(arp PUBLIC neighbor packet vgw_dpdk_io)

add_library(forward forward.cpp)
target_include_directories(forward PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(forward PUBLIC base packet session neighbor)

add_library(upstream upstream.cpp)
target_include_directories(upstream PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(upstream PUBLIC session)

add_library(control cp_shm.cpp)
target_include_directories(control PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(control PUBLIC upstream)
find_library(LIBRT rt)
if(LIBRT)
    target_link_libraries(control PUBLIC ${LIBRT})
endif()

add_library(vgw_core vgw.cpp)
target_include_directories(vgw_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vgw_core PUBLIC base vgw_dpdk_io packet session forward upstream control neighbor arp)

add_executable(vgw main.cpp)
target_link_libraries(vgw PRIVATE vgw_core)
if(TARGET gflags::gflags)
    target_link_libraries(vgw PRIVATE gflags::gflags)
else()
    target_link_libraries(vgw PRIVATE gflags)
endif()
