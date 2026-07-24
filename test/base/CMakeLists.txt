vgm_add_gtest(log_test log_test.cpp)
target_link_libraries(log_test PRIVATE base)

vgm_add_gtest(crc_test crc_test.cpp)
target_link_libraries(crc_test PRIVATE base)
