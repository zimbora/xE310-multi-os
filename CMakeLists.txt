cmake_minimum_required(VERSION 3.20)

# Only look for Zephyr when explicitly building via west (ZEPHYR_BASE is set)
if(DEFINED ENV{ZEPHYR_BASE} OR DEFINED ZEPHYR_BASE)
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
endif()

if(Zephyr_FOUND)
    set(PLATFORM_ZEPHYR TRUE)
    project(modem_controller VERSION 0.1.0 LANGUAGES CXX)

    target_sources(app PRIVATE
        src/main.cpp
        src/modem_controller.cpp
        src/at_command.cpp
        src/xe310.cpp
        src/hal/zephyr_uart.cpp
    )
    target_include_directories(app PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(app PUBLIC PLATFORM_ZEPHYR)
else()
    project(modem_controller VERSION 0.1.0 LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    option(BUILD_TESTS "Build unit tests" ON)
    option(ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)

    if(ENABLE_CPPCHECK)
        find_program(CPPCHECK cppcheck)
        if(CPPCHECK)
            set(CMAKE_CXX_CPPCHECK
                ${CPPCHECK}
                --enable=warning,style,performance,portability
                --std=c++17
                --suppress=missingIncludeSystem
                --quiet
            )
        endif()
    endif()

    add_library(modem_controller
        src/modem_controller.cpp
        src/at_command.cpp
        src/xe310.cpp
    )

    target_include_directories(modem_controller PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    if(WIN32)
        target_sources(modem_controller PRIVATE src/hal/win32_uart.cpp)
        target_compile_definitions(modem_controller PUBLIC PLATFORM_WINDOWS)
    else()
        target_sources(modem_controller PRIVATE src/hal/posix_uart.cpp)
        target_compile_definitions(modem_controller PUBLIC PLATFORM_POSIX)
    endif()

    if(BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()

    add_executable(modem_app src/main.cpp)
    target_link_libraries(modem_app PRIVATE modem_controller)
endif()
