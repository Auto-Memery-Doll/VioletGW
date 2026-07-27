# Boost.System is header-only on modern Boost (e.g. Ubuntu 26.04 / 1.90);
# requesting COMPONENTS system breaks configure when no cmake package exists.
# Use CONFIG mode to avoid CMP0167 FindBoost deprecation warning (CMake 3.30+).
find_package(Boost CONFIG REQUIRED COMPONENTS filesystem)

file(GLOB_RECURSE SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
message(STATUS "base sources: ${SOURCES}")

add_library(base ${SOURCES})

# DPDK comes from root via link_libraries(vgw_dpdk). Keep Boost PUBLIC for dependents.
target_link_libraries(base
    PUBLIC
        Boost::filesystem
)
if(TARGET Boost::system)
    target_link_libraries(base PUBLIC Boost::system)
endif()
target_include_directories(base PUBLIC ${CMAKE_SOURCE_DIR}/src)
