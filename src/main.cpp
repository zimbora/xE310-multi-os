#include "modem/modem_controller.h"
#include "modem/at_command.h"
#include "modem/uart_factory.h"
#include "modem/xe310.h"
#include "modem/log.h"
#include <memory>

MODEM_LOG_MODULE_REGISTER(modem_app);

int main() {
    auto uart = modem::create_platform_uart();
    modem::ModemController controller(std::move(uart));

    auto status = controller.connect("COM3");
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to open modem port");
        return 1;
    }

    modem::xE310 modem(controller);

    status = modem.at_ok();
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Modem not responsive");
        controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Modem initialized successfully");
    status = modem.set_echo(false);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to disable echo");
    } else {
        MODEM_LOG_INF("Echo disabled");
    }

    controller.disconnect();
    return 0;
}