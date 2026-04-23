# Zephyr module integration
# Compatible with Zephyr v4.x and NCS v3.x+

if(CONFIG_MODEM_CONTROLLER)

zephyr_library_named(modem_xe310)

zephyr_library_sources(
    ../src/at_command.cpp
    ../src/modem_controller.cpp
    ../src/xe310.cpp
    ../src/hal/zephyr_uart.cpp
)

zephyr_include_directories(../include)

zephyr_library_compile_definitions(PLATFORM_ZEPHYR)

# C++17 is enforced via prj.conf (CONFIG_STD_CPP17=y)

# Suppress warnings for C++ standard library usage in embedded context
zephyr_library_compile_options(-fno-exceptions -fno-rtti)

endif()