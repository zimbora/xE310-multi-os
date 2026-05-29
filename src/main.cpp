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

    bool net_res = network.network_connect();
    if(!net_res){
        MODEM_LOG_ERR("Failed to connect to network");
        modemController.disconnect();
        return 1;
    }
        
    bool res = network.server_connect(1, "UDP", "185.205.209.91", 10000);
    if(res){
        MODEM_LOG_INF("Connected to server successfully");
        uint8_t data[] = "Hello, World!";
        network.send_data(1, data, sizeof(data));
        MODEM_LOG_INF("Data sent successfully");
        
    }else{
        MODEM_LOG_ERR("Failed to connect to server");
        return 1;
    }

    uint32_t count = 0;
    while(true){
        network.loop();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        count++;
        if(count%5==0){
            std::string msg = "msg : " + std::to_string(count/5);
            network.send_data(1, (uint8_t*)msg.c_str(), msg.size());
        }
        if(count%30==0){
            
            if(network.server_disconnect(1))
                MODEM_LOG_INF("Disconnected from server successfully");
            else
                MODEM_LOG_ERR("Failed to disconnect from server");
            MODEM_LOG_INF("Attempting to enter sleep mode...");
            return 0;
            /*
            if(network.enter_sleep()){
                MODEM_LOG_INF("Successfully entered sleep mode");
            }else{
                MODEM_LOG_ERR("Failed to enter sleep mode");
            }
            */
        }
    }
    return 0;
}
