add_library(vgw_dpdk_io netif.cpp datapath_config.cpp)
target_include_directories(vgw_dpdk_io PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vgw_dpdk_io PUBLIC base)
