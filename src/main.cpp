#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/log.h"
#include "ipc_server.h"
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

    // IPC server: messages from external process (e.g. LwM2M agent) are
    // queued for TX. Wire format: newline-delimited text — compatible with nc.
    // Usage: nc localhost 9000   then type messages and press Enter.
    IpcServer ipc(9000, nullptr, IpcServer::Mode::line); // callback set after network is constructed

    // CoAP IPC server on port 9001: binary framed [uint16_t len LE][payload].
    // Usage: send CoAP binary frames from a LwM2M agent or test tool.
    IpcServer coap_ipc(9001, nullptr, IpcServer::Mode::framed);

    // on_data_received is a lambda so it can capture ipc and forward replies.
    auto on_data_received = [&](uint8_t cid, std::string& data, uint16_t n_bytes) {
        MODEM_LOG_INF("Data received on CID %d (%u bytes): %s", cid, n_bytes, data.c_str());
        //ipc.send(reinterpret_cast<const uint8_t*>(data.data()),static_cast<uint16_t>(n_bytes));
    };

    modem::NetworkLte network(modem, lteConfig, on_data_received);

    ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        network.tx_write(lteConfig.conn_id, data, len);
        network.call_action(modem::ModemAction::send_data);
        MODEM_LOG_INF("IPC: queued %u bytes for TX on conn %d", len, lteConfig.conn_id);
    });
    if (!ipc.start()) {
        MODEM_LOG_WRN("IPC server failed to start on port 9000 (continuing without it)");
    } else {
        MODEM_LOG_INF("IPC server listening on localhost:9000");
    }

    coap_ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        network.tx_write(lteConfig.conn_id, data, len);
        network.call_action(modem::ModemAction::send_data);
        MODEM_LOG_INF("CoAP IPC: queued %u bytes for TX on conn %d", len, lteConfig.conn_id);
    });
    if (!coap_ipc.start()) {
        MODEM_LOG_WRN("CoAP IPC server failed to start on port 9001 (continuing without it)");
    } else {
        MODEM_LOG_INF("CoAP IPC server listening on localhost:9001 (framed binary)");
    }

    // AT command passthrough server on port 9002 (line mode, nc compatible).
    // On connect  → modem enters transparent mode (raw AT).
    // On message  → forward as AT command, reply with response.
    // On disconnect → modem leaves transparent mode.
    IpcServer at_ipc(9002, nullptr, IpcServer::Mode::line);
    at_ipc.set_connect_callback([&]() {
        MODEM_LOG_INF("AT IPC: client connected, entering transparent mode");
        // Use call_action + execute_actions instead of the blocking enter_transparent_mode()
        // to avoid stalling the IPC thread before client_loop starts receiving data.
        network.enter_transparent_mode(); // this will internally call the modem action to enter transparent mode, but we need to call execute_actions here to actually perform the action and change the modem state before we start receiving AT commands from the client
    });
    at_ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        std::string cmd(reinterpret_cast<const char*>(data), len);
        std::string response;
        MODEM_LOG_INF("AT IPC >> %s", cmd.c_str());
        if (network.send_at_command(cmd, response, 5000)) {
            // parse_response() strips the status line into AtResponse::status,
            // so response.body is empty for simple commands (e.g. AT → OK).
            // Re-append the status line so the nc client sees a complete reply.
            if (!response.empty()) response += "\r\n";
            response += "OK\r\n";
            MODEM_LOG_INF("AT IPC << %s", response.c_str());
            at_ipc.send(reinterpret_cast<const uint8_t*>(response.data()),
                        static_cast<uint16_t>(response.size()));
        } else {
            const std::string err = "ERROR\r\n";
            MODEM_LOG_ERR("AT IPC: command failed");
            at_ipc.send(reinterpret_cast<const uint8_t*>(err.data()),
                        static_cast<uint16_t>(err.size()));
        }
    });
    at_ipc.set_disconnect_callback([&]() {
        MODEM_LOG_INF("AT IPC: client disconnected, leaving transparent mode");
        network.call_action(modem::ModemAction::leave_transparent_mode);
        network.execute_actions();
    });
    if (!at_ipc.start()) {
        MODEM_LOG_WRN("AT IPC server failed to start on port 9002 (continuing without it)");
    } else {
        MODEM_LOG_INF("AT IPC server listening on localhost:9002 (AT command passthrough)");
    }

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

        // Drain RX queue and forward to IPC client
        modem::QueueMessage rx_msg;
        while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
            std::string payload(rx_msg.data.begin(), rx_msg.data.end());
            MODEM_LOG_INF("RX queue [conn %d]: %s (%zu bytes)", lteConfig.conn_id, payload.c_str(), rx_msg.data.size());
            ipc.send(rx_msg.data.data(), static_cast<uint16_t>(rx_msg.data.size()));
        }
        
        /*
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
            
            if(network.enter_sleep()){
                MODEM_LOG_INF("Successfully entered sleep mode");
            }else{
                MODEM_LOG_ERR("Failed to enter sleep mode");
            }
            
        }
        */
    }
    return 0;
}
