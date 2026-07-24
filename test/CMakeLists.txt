# GoogleTest for unit tests (FetchContent if not installed).
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

function(vgm_add_gtest target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        PROPERTIES LABELS "unit"
    )
endfunction()

# Base-layer tests
add_subdirectory(base)

# Gateway business logic tests (flat layout, mirrors src/)
vgm_add_gtest(packet_test packet_test.cpp)
target_link_libraries(packet_test PRIVATE packet)

vgm_add_gtest(session_test session_test.cpp)
target_link_libraries(session_test PRIVATE session)

vgm_add_gtest(forward_test forward_test.cpp)
target_link_libraries(forward_test PRIVATE forward)

vgm_add_gtest(upstream_test upstream_test.cpp)
target_link_libraries(upstream_test PRIVATE upstream)

vgm_add_gtest(cp_shm_test cp_shm_test.cpp)
target_link_libraries(cp_shm_test PRIVATE control)

# DPDK smoke binaries (plain executables, not gtest unit tests)
add_subdirectory(dpdk)
