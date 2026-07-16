cmake_minimum_required(VERSION 3.20)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# Force Google Test to use the dynamic CRT (/MD) to match the rest of the project on MSVC
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(modem_tests
    test_at_command.cpp
    test_modem_controller.cpp
    test_xe310.cpp
)

target_link_libraries(modem_tests PRIVATE
    modem_xe310
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(modem_tests)

add_executable(test_network_lte
    test_network_lte.cpp
)

target_link_libraries(test_network_lte PRIVATE
    modem_xe310
    GTest::gtest_main
    GTest::gmock
)

gtest_discover_tests(test_network_lte)

add_executable(test_i_radio_lte
    test_i_radio_lte.cpp
)

target_include_directories(test_i_radio_lte PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_i_radio_lte PRIVATE
    modem_xe310
    GTest::gtest_main
    GTest::gmock
)

gtest_discover_tests(test_i_radio_lte)

add_executable(test_message_queue
    test_message_queue.cpp
)

target_link_libraries(test_message_queue PRIVATE
    modem_xe310
    GTest::gtest_main
)

gtest_discover_tests(test_message_queue)

add_executable(test_message_channel
    test_message_channel.cpp
)

target_link_libraries(test_message_channel PRIVATE
    modem_xe310
    GTest::gtest_main
)

gtest_discover_tests(test_message_channel)

add_executable(test_event_flags
    test_event_flags.cpp
)

target_link_libraries(test_event_flags PRIVATE
    modem_xe310
    GTest::gtest_main
)

gtest_discover_tests(test_event_flags)
