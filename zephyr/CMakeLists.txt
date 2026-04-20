# Zephyr module integration
# Add this directory as a Zephyr module in your west manifest or CMakeLists.txt

if(CONFIG_MODEM_CONTROLLER)

zephyr_library_named(modem_controller)

zephyr_library_sources(
    ../src/at_command.cpp
    ../src/modem_controller.cpp
    ../src/xe310.cpp
    ../src/hal/uart_zephyr.cpp
)

zephyr_library_include_directories(../include)

zephyr_library_compile_features(cxx_std_17)

endif()