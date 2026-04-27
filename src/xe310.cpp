#include "modem/xe310.h"

#include <cstdlib>
#include <cstring>

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

// --- SIM Card ---

ModemStatus xE310::read_iccid(std::string& iccid) {
    AtResponse response;
    auto status = controller_.send_raw("AT#CCID", response);
    if (status == ModemStatus::ok) {
        constexpr std::string_view prefix = "#CCID: ";
        if (response.body.rfind(prefix, 0) == 0) {
            iccid = response.body.substr(prefix.size());
        } else {
            iccid = response.body;
        }
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
        // Response body: "#QSS: <mode>,<status>"
        auto comma_pos = response.body.find(',');
        if (comma_pos != std::string::npos) {
            status = static_cast<SimStatus>(std::atoi(response.body.c_str() + comma_pos + 1));
        }
    }
    return result;
}

ModemStatus xE310::send_sim_command(const std::string& command, std::string& sim_response) {
    AtResponse response;
    auto cmd = "AT+CSIM=" + std::to_string(command.size()) + ",\"" + command + "\"";
    auto status = controller_.send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        sim_response = response.body;
    }
    return status;
}

// --- Network Registration ---

ModemStatus xE310::set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask,
                              uint64_t tdscdma_mask, uint64_t lte_mask_over_64) {
    AtResponse response;
    // AT#BND=<band>,<UMTS_band>,<LTE_band>,<TDSCDMA_band>,<LTE_band_over_64>
    auto cmd = "AT#BND=" + std::to_string(gsm_mask) + ","
             + std::to_string(umts_mask) + ","
             + std::to_string(lte_mask) + ","
             + std::to_string(tdscdma_mask) + ","
             + std::to_string(lte_mask_over_64);
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_bands(std::string& bands) {
    AtResponse response;
    // Response: #BND: <band>,<UMTS_band>,<LTE_band>,<TDSCDMA_band>,<LTE_band_over_64>
    auto status = controller_.send_raw("AT#BND?", response);
    if (status == ModemStatus::ok) {
        bands = response.body;
    }
    return status;
}

ModemStatus xE310::get_registration_status(RegistrationInfo& info) {
    AtResponse response;
    auto result = controller_.send_raw("AT+CEREG?", response);
    if (result != ModemStatus::ok) {
        return result;
    }

    // Response body: "+CEREG: <mode>,<stat>[,<lac>,<ci>[,<AcT>]]"
    auto colon_pos = response.body.find(':');
    if (colon_pos == std::string::npos) {
        return ModemStatus::at_error;
    }

    // Tokenize from after the colon
    std::string params = response.body.substr(colon_pos + 1);

    // Parse comma-separated fields
    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < params.size()) {
        auto comma = params.find(',', pos);
        if (comma == std::string::npos) {
            fields.push_back(params.substr(pos));
            break;
        }
        fields.push_back(params.substr(pos, comma - pos));
        pos = comma + 1;
    }

    // Trim leading/trailing whitespace and quotes from each field
    auto trim = [](const std::string& s) -> std::string {
        auto start = s.find_first_not_of(" \t\"");
        auto end = s.find_last_not_of(" \t\"");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    };

    if (fields.size() >= 2) {
        info.mode = static_cast<uint8_t>(std::atoi(trim(fields[0]).c_str()));
        info.stat = static_cast<RegStatus>(std::atoi(trim(fields[1]).c_str()));
    }

    if (fields.size() >= 4) {
        info.lac = trim(fields[2]);
        info.ci = trim(fields[3]);
        info.has_location = true;
    }

    if (fields.size() >= 5) {
        info.act = static_cast<RadioTech>(std::atoi(trim(fields[4]).c_str()));
    }

    return ModemStatus::ok;
}

ModemStatus xE310::get_signal_quality(SignalQuality& sq) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CESQ", response);
    if (status == ModemStatus::ok) {
        // Response body: "+CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>"
        auto pos = response.body.find(':');
        if (pos != std::string::npos) {
            std::sscanf(response.body.c_str() + pos + 1, " %d,%d,%*d,%*d,%d,%d",
                        &sq.rssi, &sq.ber, &sq.rsrq, &sq.rsrp);
        }
    }
    return status;
}

ModemStatus xE310::set_radio_tech(RadioTech tech) {
    AtResponse response;
    // AT+COPS=0,,,<act> — automatic selection with specific access technology
    return controller_.send_raw("AT+COPS=0,,," + std::to_string(static_cast<int>(tech)), response);
}

ModemStatus xE310::set_operator_manual(const std::string& oper, RadioTech tech) {
    AtResponse response;
    // AT+COPS=4,2,"<oper>",<act> — manual selection, numeric format, automatic fallback
    auto cmd = "AT+COPS=4,2,\"" + oper + "\"," + std::to_string(static_cast<int>(tech));
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::set_operator_auto() {
    AtResponse response;
    return controller_.send_raw("AT+COPS=0", response);
}

ModemStatus xE310::get_operator(std::string& oper) {
    AtResponse response;
    auto status = controller_.send_raw("AT+COPS?", response);
    if (status == ModemStatus::ok) {
        oper = response.body;
    }
    return status;
}

// --- Network Attach ---

ModemStatus xE310::set_apn(uint8_t cid, const std::string& apn) {
    AtResponse response;
    auto cmd = "AT+CGDCONT=" + std::to_string(cid) + ",\"IP\",\"" + apn + "\"";
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_apn(uint8_t cid, std::string& apn) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGDCONT?", response);
    if (status == ModemStatus::ok) {
        apn = response.body;
    }
    return status;
}

ModemStatus xE310::activate_pdp(uint8_t cid) {
    AtResponse response;
    return controller_.send_raw("AT+CGACT=1," + std::to_string(cid), response);
}

ModemStatus xE310::deactivate_pdp(uint8_t cid) {
    AtResponse response;
    return controller_.send_raw("AT+CGACT=0," + std::to_string(cid), response);
}

ModemStatus xE310::get_pdp_state(uint8_t cid, bool& active) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGACT?", response);
    if (status == ModemStatus::ok) {
        // Response: +CGACT: <cid>,<state>
        auto pos = response.body.find(std::to_string(cid) + ",");
        if (pos != std::string::npos) {
            active = (response.body[pos + 2] == '1');
        } else {
            active = false;
        }
    }
    return status;
}

ModemStatus xE310::get_ip_address(uint8_t cid, std::string& ip_addr) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGPADDR=" + std::to_string(cid), response);
    if (status == ModemStatus::ok) {
        // Response: +CGPADDR: <cid>,"<ip_addr>"
        auto start = response.body.find('"');
        auto end = response.body.rfind('"');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            ip_addr = response.body.substr(start + 1, end - start - 1);
        }
    }
    return status;
}

ModemStatus xE310::get_pdp_info(uint8_t cid, std::string& ip_addr, std::string& gw_addr,
                                 std::string& dns_primary, std::string& dns_secondary) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGCONTRDP=" + std::to_string(cid), response);
    if (status == ModemStatus::ok) {
        // Response: +CGCONTRDP: <cid>,<bearer_id>,"<apn>","<ip>","<gw>","<dns1>","<dns2>"
        // Extract quoted fields
        size_t pos = 0;
        int field = 0;
        while (pos < response.body.size()) {
            auto start = response.body.find('"', pos);
            if (start == std::string::npos) break;
            auto end = response.body.find('"', start + 1);
            if (end == std::string::npos) break;

            auto value = response.body.substr(start + 1, end - start - 1);
            switch (field) {
                case 1: ip_addr = value; break;
                case 2: gw_addr = value; break;
                case 3: dns_primary = value; break;
                case 4: dns_secondary = value; break;
            }
            ++field;
            pos = end + 1;
        }
    }
    return status;
}

// --- UDP Connection ---

ModemStatus xE310::udp_open(uint8_t conn_id, const std::string& host, uint16_t remote_port,
                             uint16_t local_port, uint8_t cid) {
    AtResponse response;
    // AT#SD=<connId>,1,<remotePort>,"<host>",0,<localPort>,1,<cid>
    // Protocol 1 = UDP, closure type 0 = local disconnect, data mode 1 = command mode
    auto cmd = "AT#SD=" + std::to_string(conn_id) + ",1,"
             + std::to_string(remote_port) + ",\""
             + host + "\",0,"
             + std::to_string(local_port) + ",1,"
             + std::to_string(cid);
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::udp_listen(uint8_t conn_id, uint16_t local_port, uint8_t cid) {
    AtResponse response;
    // AT#SL=<connId>,1,<listenPort>,255,<cid>
    // State 1 = enable listening, 255 = accept from any IP
    auto cmd = "AT#SL=" + std::to_string(conn_id) + ",1,"
             + std::to_string(local_port) + ",255,"
             + std::to_string(cid);
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::udp_send(uint8_t conn_id, const std::vector<uint8_t>& data) {
    AtResponse response;
    // AT#SSENDEXT=<connId>,<bytesToSend>
    // Modem returns "\r\n> " prompt, then we send raw bytes.
    // After <bytesToSend> bytes are received, modem returns OK.
    auto cmd = "AT#SSENDEXT=" + std::to_string(conn_id) + "," + std::to_string(data.size());
    return controller_.send_with_prompt(cmd, data, response);
}

ModemStatus xE310::udp_receive(uint8_t conn_id, std::vector<uint8_t>& data, uint16_t max_bytes) {
    AtResponse response;
    // AT#SRECV=<connId>,<maxBytes>
    auto cmd = "AT#SRECV=" + std::to_string(conn_id) + "," + std::to_string(max_bytes);
    auto status = controller_.send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        // Response: #SRECV: <connId>,<recvDataLen>\r\n<data>
        // Find the data after the first \n
        auto nl_pos = response.body.find('\n');
        if (nl_pos != std::string::npos && nl_pos + 1 < response.body.size()) {
            auto payload = response.body.substr(nl_pos + 1);
            data.assign(payload.begin(), payload.end());
        } else {
            data.clear();
        }
    }
    return status;
}

ModemStatus xE310::udp_close(uint8_t conn_id) {
    AtResponse response;
    return controller_.send_raw("AT#SH=" + std::to_string(conn_id), response);
}

ModemStatus xE310::udp_status(uint8_t conn_id, uint8_t& state) {
    AtResponse response;
    auto status = controller_.send_raw("AT#SS=" + std::to_string(conn_id), response);
    if (status == ModemStatus::ok) {
        // Response: #SS: <connId>,<state>
        auto comma_pos = response.body.find(',');
        if (comma_pos != std::string::npos) {
            state = static_cast<uint8_t>(std::atoi(response.body.c_str() + comma_pos + 1));
        }
    }
    return status;
}

} // namespace modem
