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

    bool res = network.server_connect(1, "UDP", "example.com", 1234);
    if(res){
        MODEM_LOG_INF("Connected to server successfully");
        uint8_t data[] = "Hello, World!";
        network.send_data(1, data, sizeof(data));
        MODEM_LOG_INF("Data sent successfully");
        std::this_thread::sleep_for(std::chrono::seconds(5)); // wait for a bit before disconnecting to allow any responses to be received and printed
        network.server_disconnect(1);
        MODEM_LOG_INF("Disconnected from server successfully");
    }else{
        MODEM_LOG_ERR("Failed to connect to server");
    }
    return 0;
}
