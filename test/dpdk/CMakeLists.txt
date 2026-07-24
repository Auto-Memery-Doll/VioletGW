# DPDK EAL smoke (no base dependency beyond project-wide fg_dpdk).
add_executable(eal_smoke eal_smoke.c)

# Real mbuf + Forwarder forward/reverse NAT (mempool only; no eth port).
add_executable(fwd_loop_smoke fwd_loop_smoke.cpp)
target_link_libraries(fwd_loop_smoke PRIVATE forward)
