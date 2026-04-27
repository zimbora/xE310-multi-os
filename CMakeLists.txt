cmake_minimum_required(VERSION 3.5)

# Zephyr build: find_package(Zephyr) must precede project() to set up the toolchain
if(DEFINED ZEPHYR_BASE OR DEFINED ENV{ZEPHYR_BASE})
    set(CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/zephyr/prj.conf)
    set(EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR})
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(modem_xe310 VERSION 1.0.0)
    target_sources(app PRIVATE src/main.cpp)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(app PRIVATE modem_xe310)
    return()
endif()

project(modem_xe310 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MODEM_BUILD_TESTS "Build unit tests" ON)
option(MODEM_BUILD_SHARED "Build shared library" OFF)

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
)

# --- HAL sources (platform-specific) ---
if(MODEM_PLATFORM STREQUAL "zephyr")
    list(APPEND MODEM_PLATFORM_SOURCES src/hal/zephyr_uart.cpp)
elseif(MODEM_PLATFORM STREQUAL "windows")
    list(APPEND MODEM_PLATFORM_SOURCES src/hal/win32_uart.cpp)
else()
    list(APPEND MODEM_PLATFORM_SOURCES src/hal/posix_uart.cpp)
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
    add_executable(modem_app src/main.cpp)
    target_link_libraries(modem_app PRIVATE modem_xe310)
endif()

# --- Tests (desktop only) ---
if(MODEM_BUILD_TESTS AND NOT MODEM_PLATFORM STREQUAL "zephyr")
    enable_testing()
    add_subdirectory(tests)
endif()
