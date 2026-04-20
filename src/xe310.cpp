#include "modem/xe310.h"

#include <cstdlib>

namespace modem {

xE310::xE310(ModemController& controller)
    : controller_(controller) {}

ModemStatus xE310::set_baudrate(uint32_t baudrate) {
    AtResponse response;
    return controller_.send_raw("AT+IPR=" + std::to_string(baudrate), response);
}

ModemStatus xE310::set_echo(bool enable) {
    AtResponse response;
    return controller_.send_raw(enable ? "ATE1" : "ATE0", response);
}

ModemStatus xE310::at_ok() {
    AtResponse response;
    return controller_.send_raw("AT", response);
}

ModemStatus xE310::request_imei_sv(std::string& imei_sv) {
    AtResponse response;
    auto status = controller_.send_raw("AT+IMEISV", response);
    if (status == ModemStatus::ok) {
        imei_sv = response.body;
    }
    return status;
}

ModemStatus xE310::request_model_id(std::string& model) {
    AtResponse response;
    auto status = controller_.send_raw("AT#CGMM", response);
    if (status == ModemStatus::ok) {
        model = response.body;
    }
    return status;
}

ModemStatus xE310::request_telit_id(std::string& tid) {
    AtResponse response;
    auto status = controller_.send_raw("AT#TID", response);
    if (status == ModemStatus::ok) {
        tid = response.body;
    }
    return status;
}

ModemStatus xE310::request_identification(std::string& info) {
    AtResponse response;
    auto status = controller_.send_raw("ATI", response);
    if (status == ModemStatus::ok) {
        info = response.body;
    }
    return status;
}

ModemStatus xE310::read_iccid(std::string& iccid) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CCID", response);
    if (status == ModemStatus::ok) {
        iccid = response.body;
    }
    return status;
}

ModemStatus xE310::read_imsi(std::string& imsi) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CIMI", response);
    if (status == ModemStatus::ok) {
        imsi = response.body;
    }
    return status;
}

ModemStatus xE310::set_sim_detection(SimDetMode mode) {
    AtResponse response;
    return controller_.send_raw("AT#SIMDET=" + std::to_string(static_cast<int>(mode)), response);
}

ModemStatus xE310::query_sim_status(SimStatus& status) {
    AtResponse response;
    auto result = controller_.send_raw("AT#QSS?", response);
    if (result == ModemStatus::ok) {
        // Response: #QSS: <mode>,<status>
        auto pos = response.body.find(',');
        if (pos != std::string::npos && pos + 1 < response.body.size()) {
            int val = std::atoi(response.body.c_str() + pos + 1);
            status = static_cast<SimStatus>(val);
        }
    }
    return result;
}

ModemStatus xE310::send_sim_command(const std::string& command, std::string& sim_response) {
    AtResponse response;
    auto cmd = "AT+CSIM=" + std::to_string(command.size()) + ",\"" + command + "\"";
    auto status = controller_.send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        // Response: +CSIM: <length>,"<response>"
        auto pos = response.body.find('"');
        auto end = response.body.rfind('"');
        if (pos != std::string::npos && end != pos) {
            sim_response = response.body.substr(pos + 1, end - pos - 1);
        } else {
            sim_response = response.body;
        }
    }
    return status;
}

ModemStatus xE310::set_apn(uint8_t cid, const std::string& apn) {
    AtResponse response;
    auto cmd = "AT+CGDCONT=" + std::to_string(cid) + ",\"IP\",\"" + apn + "\"";
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_apn(uint8_t cid, std::string& apn) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGDCONT?", response);
    if (status == ModemStatus::ok) {
        // Find the line matching the requested cid
        auto cid_str = std::to_string(cid);
        auto pos = response.body.find("+CGDCONT: " + cid_str);
        if (pos != std::string::npos) {
            // Format: +CGDCONT: <cid>,"<type>","<apn>",...
            auto first_q = response.body.find('"', pos);
            if (first_q != std::string::npos) {
                auto second_q = response.body.find('"', first_q + 1);
                auto third_q = response.body.find('"', second_q + 1);
                auto fourth_q = response.body.find('"', third_q + 1);
                if (third_q != std::string::npos && fourth_q != std::string::npos) {
                    apn = response.body.substr(third_q + 1, fourth_q - third_q - 1);
                }
            }
        }
    }
    return status;
}

ModemStatus xE310::set_radio_tech(RadioTech tech) {
    AtResponse response;
    return controller_.send_raw("AT#WS46=" + std::to_string(static_cast<int>(tech)), response);
}

ModemStatus xE310::set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask) {
    AtResponse response;
    auto cmd = "AT#BND=" + std::to_string(gsm_mask) + ","
             + std::to_string(umts_mask) + ","
             + std::to_string(lte_mask);
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_bands(std::string& bands) {
    AtResponse response;
    auto status = controller_.send_raw("AT#BND?", response);
    if (status == ModemStatus::ok) {
        bands = response.body;
    }
    return status;
}

ModemStatus xE310::get_registration_status(RegStatus& status) {
    AtResponse response;
    auto result = controller_.send_raw("AT+CEREG?", response);
    if (result == ModemStatus::ok) {
        // Response: +CEREG: <n>,<stat>
        auto pos = response.body.find(',');
        if (pos != std::string::npos && pos + 1 < response.body.size()) {
            int val = std::atoi(response.body.c_str() + pos + 1);
            status = static_cast<RegStatus>(val);
        }
    }
    return result;
}

ModemStatus xE310::get_signal_quality(int& rssi, int& ber) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CSQ", response);
    if (status == ModemStatus::ok) {
        // Response: +CSQ: <rssi>,<ber>
        auto pos = response.body.find(':');
        if (pos != std::string::npos) {
            rssi = std::atoi(response.body.c_str() + pos + 1);
            auto comma = response.body.find(',', pos);
            if (comma != std::string::npos) {
                ber = std::atoi(response.body.c_str() + comma + 1);
            }
        }
    }
    return status;
}

ModemStatus xE310::get_operator(std::string& oper) {
    AtResponse response;
    auto status = controller_.send_raw("AT+COPS?", response);
    if (status == ModemStatus::ok) {
        // Response: +COPS: <mode>,<format>,"<oper>",<act>
        auto pos = response.body.find('"');
        auto end = response.body.rfind('"');
        if (pos != std::string::npos && end != pos) {
            oper = response.body.substr(pos + 1, end - pos - 1);
        }
    }
    return status;
}

ModemStatus xE310::set_operator_auto() {
    AtResponse response;
    return controller_.send_raw("AT+COPS=0", response);
}

} // namespace modem
