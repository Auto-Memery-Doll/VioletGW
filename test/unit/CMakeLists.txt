# Unit tests (no PCI / no real eth ports).
add_subdirectory(base)

vgw_add_gtest(packet_test packet_test.cpp)
target_link_libraries(packet_test PRIVATE packet)

vgw_add_gtest(session_test session_test.cpp)
target_link_libraries(session_test PRIVATE session)

vgw_add_gtest(forward_test forward_test.cpp)
target_link_libraries(forward_test PRIVATE forward)

vgw_add_gtest(upstream_test upstream_test.cpp)
target_link_libraries(upstream_test PRIVATE upstream)

vgw_add_gtest(cp_shm_test cp_shm_test.cpp)
target_link_libraries(cp_shm_test PRIVATE control)

vgw_add_gtest(vgw_test vgw_test.cpp)
target_link_libraries(vgw_test PRIVATE vgw_core)
