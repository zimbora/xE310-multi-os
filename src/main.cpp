#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/log.h"
#include <memory>

#include <thread>
#include <chrono>

MODEM_LOG_MODULE_REGISTER(modem_app);

int main() {
    
    
    auto uart = modem::create_platform_uart();
    modem::ModemController modemController(std::move(uart));
    
    modemController.connect("COM17", modem::UartConfig{});

    modem::xE310 modem(modemController);
    auto status = modem.at_ok();
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Modem not responsive");
        modemController.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Modem initialized successfully");

    modem::NetworkLteConfig lteConfig;
    modem::NetworkLte network(modem, lteConfig);

    while(true){
        network.log_state();
        auto state = network.st_machine();
        MODEM_LOG_INF("try count: %d", network.try_count());
        if(state == modem::NetworkLteState::done){
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        //delay_ms(1000); // Add a delay to avoid busy looping, adjust as needed
    }
    return 0;
}
