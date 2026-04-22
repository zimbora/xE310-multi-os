# Zephyr module integration
# Compatible with Zephyr v4.x and NCS v3.x+

if(CONFIG_MODEM_CONTROLLER)

zephyr_library_named(modem_xe310)

zephyr_library_sources(
    ../src/at_command.cpp
    ../src/modem_controller.cpp
    ../src/xe310.cpp
    ../src/hal/uart_zephyr.cpp
)

zephyr_library_include_directories(../include)

# Zephyr 4.x supports cxx_std_17 via compile_features
zephyr_library_compile_features(cxx_std_17)

# Suppress warnings for C++ standard library usage in embedded context
zephyr_library_compile_options(-fno-exceptions -fno-rtti)

endif()