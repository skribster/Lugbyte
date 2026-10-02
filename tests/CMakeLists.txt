set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

add_subdirectory(gtest)

add_executable(luggage_tests)
target_sources(luggage_tests
    PRIVATE
        tests.cpp
)


target_link_libraries(luggage_tests
    PRIVATE
        luggage_core
        GTest::gtest_main
)

target_compile_definitions(luggage_tests
    PRIVATE
        LUG_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

include(GoogleTest)
gtest_discover_tests(luggage_tests)