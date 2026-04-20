cmake_minimum_required(VERSION 3.14)
project(xE310 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Library ───────────────────────────────────────────────────────────────────

set(XE310_SOURCES
    src/xE310.cpp
)

if(UNIX)
    list(APPEND XE310_SOURCES src/serial/SerialPosix.cpp)
elseif(WIN32)
    list(APPEND XE310_SOURCES src/serial/SerialWindows.cpp)
endif()

add_library(xE310 STATIC ${XE310_SOURCES})
target_include_directories(xE310 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serial)

if(WIN32)
    # No extra libs needed – Win32 API is always available
elseif(UNIX)
    # On some Linux distributions you may need to link against -lrt
    # target_link_libraries(xE310 PRIVATE rt)
endif()

# ── Linux / macOS example ─────────────────────────────────────────────────────

if(UNIX)
    add_executable(xE310_example examples/linux/basic/main.cpp)
    target_link_libraries(xE310_example PRIVATE xE310)
    target_include_directories(xE310_example PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/serial)
endif()

# ── Windows example ───────────────────────────────────────────────────────────

if(WIN32)
    add_executable(xE310_example examples/windows/basic/main.cpp)
    target_link_libraries(xE310_example PRIVATE xE310)
    target_include_directories(xE310_example PRIVATE src)
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS xE310
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(FILES
        src/xE310.hpp
        src/platform.hpp
        src/serial/ISerial.hpp
        src/serial/SerialArduino.hpp
        src/serial/SerialPosix.hpp
        src/serial/SerialWindows.hpp
        DESTINATION include/xE310)
