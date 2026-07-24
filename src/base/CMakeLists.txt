find_package(Boost REQUIRED COMPONENTS filesystem system)

file(GLOB_RECURSE SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
message(STATUS "base sources: ${SOURCES}")

add_library(base ${SOURCES})

# DPDK comes from root via link_libraries(fg_dpdk). Keep Boost PUBLIC for dependents.
target_link_libraries(base
    PUBLIC
        Boost::filesystem
        Boost::system
)
target_include_directories(base PUBLIC ${CMAKE_SOURCE_DIR}/src)
