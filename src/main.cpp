#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/log.h"
#include <memory>

#include <thread>
#include <chrono>

MODEM_LOG_MODULE_REGISTER(modem_app);

void on_data_received(uint8_t cid, std::string& data, uint16_t n_bytes) {
    MODEM_LOG_INF("Data received on CID %d (%u bytes): %s", cid, n_bytes, data.c_str());
}

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
    modem::NetworkLte network(modem, lteConfig, on_data_received);

    bool net_res = network.network_connect();
    if(!net_res){
        MODEM_LOG_ERR("Failed to connect to network");
        modemController.disconnect();
        return 1;
    }
        
    bool res = network.server_connect(lteConfig.conn_id, "UDP", "185.205.209.91", 10000);
    if(res){
        MODEM_LOG_INF("Connected to server successfully");
        // Queue initial message for TX
        std::string hello = "Hello, World!";
        network.tx_write(lteConfig.conn_id, reinterpret_cast<const uint8_t*>(hello.data()), hello.size());
        MODEM_LOG_INF("Initial message queued for TX");
    }else{
        MODEM_LOG_ERR("Failed to connect to server");
        return 1;
    }

    uint32_t count = 0;
    while(true){
        network.loop();

        // Drain RX queue for our connection
        modem::QueueMessage rx_msg;
        while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
            std::string payload(rx_msg.data.begin(), rx_msg.data.end());
            MODEM_LOG_INF("RX queue [conn %d]: %s (%zu bytes)", lteConfig.conn_id, payload.c_str(), rx_msg.data.size());
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        count++;

        // Queue a TX message every 5 seconds
        if(count%5==0){
            std::string msg = "msg : " + std::to_string(count/5);
            network.tx_write(lteConfig.conn_id, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
            MODEM_LOG_INF("Queued TX message: %s", msg.c_str());
            // Trigger send_data action to drain TX queues
            network.call_action(modem::ModemAction::send_data);
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
