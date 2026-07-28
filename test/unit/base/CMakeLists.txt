vgw_add_gtest(log_test log_test.cpp)
target_link_libraries(log_test PRIVATE base)

vgw_add_gtest(crc_test crc_test.cpp)
target_link_libraries(crc_test PRIVATE base)

vgw_add_gtest(pkt_filter_test pkt_filter_test.cpp)
target_link_libraries(pkt_filter_test PRIVATE base)
