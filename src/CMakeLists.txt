set(SRC ${CMAKE_SOURCE_DIR}/src)

add_subdirectory(${SRC}/base ${BUILD_DIR}/Base)

add_library(packet packet.cpp)
target_include_directories(packet PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(session session.cpp)
target_include_directories(session PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_library(forward forward.cpp)
target_include_directories(forward PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(forward PUBLIC packet session)

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

set(FLOW_GW_SOURCES
    main.cpp
    dpdk_netif.cpp
)

add_executable(flow_gw ${FLOW_GW_SOURCES})
target_link_libraries(flow_gw base packet session forward upstream control)
