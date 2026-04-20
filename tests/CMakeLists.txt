cmake_minimum_required(VERSION 3.20)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

add_executable(modem_tests
    test_at_command.cpp
    test_modem_controller.cpp
)

target_link_libraries(modem_tests PRIVATE
    modem_controller
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(modem_tests)
