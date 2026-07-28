# DPDK smoke binaries (plain executables; not registered as ctest unit tests).
#
# Examples:
#   ./bin/Test/dpdk/eal_smoke -l 0 --no-huge --no-pci
#   sudo ./bin/Test/dpdk/eal_smoke -l 0 -a 0000:02:03.0
#   ./bin/Test/dpdk/fwd_loop_smoke -l 0 --no-huge --no-pci --no-shconf

add_executable(eal_smoke eal_smoke.c)

add_executable(fwd_loop_smoke fwd_loop_smoke.cpp)
target_include_directories(fwd_loop_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fwd_loop_smoke PRIVATE vgw_core)
