cmake_minimum_required(VERSION 3.5)

# Zephyr build: find_package(Zephyr) must precede project() to set up the toolchain
if(DEFINED ZEPHYR_BASE OR DEFINED ENV{ZEPHYR_BASE})
    set(CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/zephyr/prj.conf)
    set(EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR})
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(modem_xe310 VERSION 1.0.0)
    target_sources(app PRIVATE src/zephyr_main.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(app PRIVATE modem_xe310)
    return()
endif()

project(modem_xe310 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MODEM_BUILD_TESTS "Build unit tests" ON)
option(MODEM_BUILD_SHARED "Build shared library" OFF)

set(MODEM_LOG_LEVEL "3" CACHE STRING "Compile-time modem log level (0=none,1=err,2=wrn,3=inf,4=dbg)")
set_property(CACHE MODEM_LOG_LEVEL PROPERTY STRINGS 0 1 2 3 4)
set(NETWORK_LOG_LEVEL "3" CACHE STRING "Compile-time network log level (0=none,1=err,2=wrn,3=inf,4=dbg)")
set_property(CACHE NETWORK_LOG_LEVEL PROPERTY STRINGS 0 1 2 3 4)

if(NOT MODEM_LOG_LEVEL MATCHES "^[0-4]$")
    message(FATAL_ERROR "MODEM_LOG_LEVEL must be 0..4")
endif()

if(NOT NETWORK_LOG_LEVEL MATCHES "^[0-4]$")
    message(FATAL_ERROR "NETWORK_LOG_LEVEL must be 0..4")
endif()

# --- Detect platform ---
if(CONFIG_ZEPHYR)
    set(MODEM_PLATFORM "zephyr")
elseif(WIN32)
    set(MODEM_PLATFORM "windows")
elseif(APPLE)
    set(MODEM_PLATFORM "macos")
elseif(UNIX)
    set(MODEM_PLATFORM "linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "Modem xE310 — platform: ${MODEM_PLATFORM}")

# --- Core sources (platform-independent) ---
set(MODEM_CORE_SOURCES
    src/at_command.cpp
    src/modem_controller.cpp
    src/xe310.cpp
    src/network_lte.cpp
    src/i_radio_lte.cpp
)

# --- HAL sources (platform-specific) ---
if(MODEM_PLATFORM STREQUAL "zephyr")
    list(APPEND MODEM_PLATFORM_SOURCES
        src/hal/zephyr_uart.cpp
        src/hal/zephyr_timer.cpp
        src/hal/zephyr_message_queue.cpp
        src/hal/zephyr_message_channel.cpp
        src/hal/zephyr_event_flags.cpp
    )
elseif(MODEM_PLATFORM STREQUAL "windows")
    list(APPEND MODEM_PLATFORM_SOURCES
        src/hal/win32_uart.cpp
        src/hal/win32_timer.cpp
        src/hal/win32_message_queue.cpp
        src/hal/win32_message_channel.cpp
        src/hal/win32_event_flags.cpp
    )
else()
    list(APPEND MODEM_PLATFORM_SOURCES
        src/hal/posix_uart.cpp
        src/hal/posix_timer.cpp
        src/hal/posix_message_queue.cpp
        src/hal/posix_message_channel.cpp
        src/hal/posix_event_flags.cpp
    )
endif()

# --- Library target ---
if(MODEM_BUILD_SHARED)
    add_library(modem_xe310 SHARED ${MODEM_CORE_SOURCES} ${MODEM_PLATFORM_SOURCES})
else()
    add_library(modem_xe310 STATIC ${MODEM_CORE_SOURCES} ${MODEM_PLATFORM_SOURCES})
endif()

target_include_directories(modem_xe310
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(modem_xe310 PUBLIC cxx_std_17)
target_compile_definitions(modem_xe310
    PUBLIC
        MODEM_LOG_LEVEL=${MODEM_LOG_LEVEL}
        NETWORK_LOG_LEVEL=${NETWORK_LOG_LEVEL}
)

# --- Platform-specific link dependencies ---
if(MODEM_PLATFORM STREQUAL "windows")
    target_compile_definitions(modem_xe310 PRIVATE MODEM_PLATFORM_WINDOWS)
elseif(MODEM_PLATFORM STREQUAL "macos")
    target_compile_definitions(modem_xe310 PRIVATE MODEM_PLATFORM_POSIX)
    find_library(IOKIT_FRAMEWORK IOKit)
    target_link_libraries(modem_xe310 PRIVATE ${IOKIT_FRAMEWORK})
elseif(MODEM_PLATFORM STREQUAL "linux")
    target_compile_definitions(modem_xe310 PRIVATE MODEM_PLATFORM_POSIX)
elseif(MODEM_PLATFORM STREQUAL "zephyr")
    target_compile_definitions(modem_xe310 PRIVATE MODEM_PLATFORM_ZEPHYR)
endif()

# --- Install rules ---
include(GNUInstallDirs)

install(TARGETS modem_xe310
    EXPORT modem_xe310-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/modem
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT modem_xe310-targets
    FILE modem_xe310-config.cmake
    NAMESPACE modem::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/modem_xe310
)

# --- Application executable (desktop only) ---
if(NOT MODEM_PLATFORM STREQUAL "zephyr")
    if(MODEM_PLATFORM STREQUAL "windows")
        set(IPC_SOURCE src/win32_ipc_server.cpp)
    else()
        set(IPC_SOURCE src/posix_ipc_server.cpp)
    endif()

    add_executable(modem_app src/main.cpp ${IPC_SOURCE})
    target_include_directories(modem_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(modem_app PRIVATE modem_xe310)
    if(MODEM_PLATFORM STREQUAL "windows")
        target_link_libraries(modem_app PRIVATE Ws2_32)
    endif()

    add_executable(test_xe310 src/test_xe310.cpp)
    target_link_libraries(test_xe310 PRIVATE modem_xe310)
endif()

# --- Tests (desktop only) ---
if(MODEM_BUILD_TESTS AND NOT MODEM_PLATFORM STREQUAL "zephyr")
    enable_testing()
    add_subdirectory(tests)
endif()
