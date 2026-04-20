#include "modem/modem_controller.h"
#include "modem/at_command.h"
#include "modem/uart_factory.h"
#include <iostream>
#include <memory>

int main() {
    auto uart = modem::create_platform_uart();
    modem::ModemController controller(std::move(uart));

    auto status = controller.connect("COM3");
    if (status != modem::ModemStatus::ok) {
        std::cerr << "Failed to open modem port" << std::endl;
        return 1;
    }

    modem::AtResponse response;
    status = controller.send_raw("AT", response);
    if (status != modem::ModemStatus::ok) {
        std::cerr << "Modem not responsive" << std::endl;
        controller.disconnect();
        return 1;
    }

    std::cout << "Modem initialized successfully" << std::endl;
    std::cout << "Response: " << response.body << std::endl;

    controller.disconnect();
    return 0;
}