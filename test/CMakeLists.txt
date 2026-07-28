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

function(vgw_add_gtest target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        PROPERTIES LABELS "unit"
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(dpdk)
