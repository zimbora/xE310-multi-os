#include "modem/xe310.h"
#include "modem/at_command.h"
#include "modem/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace modem {

/// Parses a standard AT response body of the form "<prefix>: <f0>,<f1>,..."
/// Returns a vector of trimmed (leading/trailing whitespace and quotes stripped) field strings.
/// Returns an empty vector if no ':' separator is found in body.
static std::vector<std::string> parse_at_params(const std::string& body) {
    auto colon = body.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    std::vector<std::string> fields;
    const std::string params = body.substr(colon + 1);
    size_t pos = 0;
    while (pos <= params.size()) {
        auto comma = params.find(',', pos);
        auto token = (comma == std::string::npos)
                     ? params.substr(pos)
                     : params.substr(pos, comma - pos);
        auto start = token.find_first_not_of(" \t\"");
        auto end   = token.find_last_not_of(" \t\"");
        fields.push_back(start == std::string::npos ? "" : token.substr(start, end - start + 1));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return fields;
}

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
        constexpr std::string_view prefix = "#CGMM: ";
        if (response.body.rfind(prefix, 0) == 0) {
            model = response.body.substr(prefix.size());
        } else {
            model = response.body;
        }
    }
    return status;
}

ModemStatus xE310::request_sw_package_version(SoftwarePackageVersion& ver) {
    AtResponse response;
    auto status = controller_.send_raw("AT#SWPKGV", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Body contains 4 lines joined by AT_TERMINATOR
    std::string_view body = response.body;
    std::string_view terminator = AT_TERMINATOR;
    std::string* fields[] = { &ver.package_version, &ver.modem_version,
                               &ver.prod_params_version, &ver.app_version };
    std::string_view::size_type start = 0;
    for (auto* field : fields) {
        auto end = body.find(terminator, start);
        *field = (end == std::string_view::npos)
                 ? std::string(body.substr(start))
                 : std::string(body.substr(start, end - start));
        if (end == std::string_view::npos) break;
        start = end + terminator.size();
    }

    return ModemStatus::ok;
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

ModemStatus xE310::get_imei(std::string& imei) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CGSN", response);
    if (status == ModemStatus::ok) {
        imei = response.body;
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
        auto fields = parse_at_params(response.body);
        if (fields.size() >= 2) {
            status = static_cast<SimStatus>(std::atoi(fields[1].c_str()));
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

// --- PSM ---

ModemStatus xE310::set_psm(const CpsmsConfig& cfg) {
    AtResponse response;
    // AT+CPSMS=<mode>[,<ReqPeriodicRAU>[,<ReqGPRSreadyTimer>[,<ReqPeriodicTAU>[,<ReqActiveTime>]]]]
    // Empty optional fields are left blank between commas, timer values are quoted binary strings.
    std::string cmd = "AT+CPSMS=" + std::to_string(static_cast<int>(cfg.mode));
    if (!cfg.req_periodic_rau.empty() || !cfg.req_gprs_ready_timer.empty() ||
        !cfg.req_periodic_tau.empty() || !cfg.req_active_time.empty()) {
        cmd += ",";
        if (!cfg.req_periodic_rau.empty())      cmd += "\"" + cfg.req_periodic_rau + "\"";
        cmd += ",";
        if (!cfg.req_gprs_ready_timer.empty())  cmd += "\"" + cfg.req_gprs_ready_timer + "\"";
        cmd += ",";
        if (!cfg.req_periodic_tau.empty())      cmd += "\"" + cfg.req_periodic_tau + "\"";
        cmd += ",";
        if (!cfg.req_active_time.empty())       cmd += "\"" + cfg.req_active_time + "\"";
    }
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_psm(CpsmsConfig& cfg) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CPSMS?", response);
    if (status != ModemStatus::ok) {
        return status;
    }
    // Response body: "+CPSMS: <mode>,[<RAU>],[<GPRSTimer>],[<TAU>],[<ActiveTime>]"
    auto fields = parse_at_params(response.body);
    if (fields.empty()) {
        return ModemStatus::at_error;
    }

    if (fields.size() >= 1) cfg.mode                  = static_cast<PsmMode>(std::atoi(fields[0].c_str()));
    if (fields.size() >= 2) cfg.req_periodic_rau       = fields[1];
    if (fields.size() >= 3) cfg.req_gprs_ready_timer   = fields[2];
    if (fields.size() >= 4) cfg.req_periodic_tau       = fields[3];
    if (fields.size() >= 5) cfg.req_active_time        = fields[4];

    return ModemStatus::ok;
}

ModemStatus xE310::disable_psm() {
    AtResponse response;
    return controller_.send_raw("AT+CPSMS=0", response);
}

ModemStatus xE310::set_telit_psm(const TelitCpsmsConfig& cfg) {
    AtResponse response;
    // AT#CPSMS=<mode>[,<RAU>[,<GPRSTimer>[,<TAU>[,<ActiveTime>[,<psmVersion>[,<psmThreshold>]]]]]]
    std::string cmd = "AT#CPSMS=" + std::to_string(static_cast<int>(cfg.mode));

    // Determine last field that has a value so we know how far to extend the command
    int last = 0;
    if (cfg.has_periodic_rau)    last = 1;
    if (cfg.has_gprs_ready_timer) last = 2;
    if (cfg.has_periodic_tau)    last = 3;
    if (cfg.has_active_time)     last = 4;
    if (cfg.has_psm_version)     last = 5;
    if (cfg.has_psm_threshold)   last = 6;

    auto append_opt = [&](int field, bool has, uint32_t val) {
        if (field <= last) {
            cmd += ",";
            if (has) cmd += std::to_string(val);
        }
    };

    append_opt(1, cfg.has_periodic_rau,    cfg.req_periodic_rau);
    append_opt(2, cfg.has_gprs_ready_timer, cfg.req_gprs_ready_timer);
    append_opt(3, cfg.has_periodic_tau,    cfg.req_periodic_tau);
    append_opt(4, cfg.has_active_time,     cfg.req_active_time);
    append_opt(5, cfg.has_psm_version,     static_cast<uint32_t>(cfg.psm_version));
    append_opt(6, cfg.has_psm_threshold,   cfg.psm_threshold);

    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_telit_psm(TelitCpsmsStatus& st) {
    AtResponse response;
    auto result = controller_.send_raw("AT#CPSMS?", response);
    if (result != ModemStatus::ok) {
        return result;
    }
    // Response body: "#CPSMS: <status>,[<T3324>],[<T3412>],<psmVersion>,<psmThreshold>,<mode>"
    auto fields = parse_at_params(response.body);
    if (fields.empty()) {
        return ModemStatus::at_error;
    }

    if (fields.size() >= 1) st.status       = static_cast<uint8_t>(std::atoi(fields[0].c_str()));
    if (fields.size() >= 2 && !fields[1].empty()) st.t3324 = static_cast<uint32_t>(std::atol(fields[1].c_str()));
    if (fields.size() >= 3 && !fields[2].empty()) st.t3412 = static_cast<uint32_t>(std::atol(fields[2].c_str()));
    if (fields.size() >= 4) st.psm_version  = static_cast<uint8_t>(std::atoi(fields[3].c_str()));
    if (fields.size() >= 5) st.psm_threshold = static_cast<uint32_t>(std::atol(fields[4].c_str()));
    if (fields.size() >= 6) st.mode          = static_cast<PsmMode>(std::atoi(fields[5].c_str()));

    return ModemStatus::ok;
}

ModemStatus xE310::disable_telit_psm() {
    AtResponse response;
    return controller_.send_raw("AT#CPSMS=", response);
}

ModemStatus xE310::set_psm_urc(bool enable) {
    AtResponse response;
    return controller_.send_raw(std::string("AT#PSMURC=") + (enable ? "1" : "0"), response);
}

ModemStatus xE310::get_psm_urc(bool& enabled) {
    AtResponse response;
    auto status = controller_.send_raw("AT#PSMURC?", response);
    if (status == ModemStatus::ok) {
        // Response body: "#PSMURC: <en>"
        auto fields = parse_at_params(response.body);
        if (!fields.empty()) {
            enabled = std::atoi(fields[0].c_str()) != 0;
        }
    }
    return status;
}

// --- Power ---

void xE310::power_on() {
    // TODO: assert power-enable GPIO

}

void xE310::power_off() {
    // TODO: de-assert power-enable GPIO
}

ModemStatus xE310::power_radio() {
    AtResponse response;
    // GPIO -> wake0
    return controller_.send_raw("AT+CFUN=1", response, 15000);
}

ModemStatus xE310::power_off_radio() {
    AtResponse response;
    return controller_.send_raw("AT+CFUN=0", response, 15000);
}

ModemStatus xE310::shutdown() {
    AtResponse response;
    return controller_.send_raw("AT#SHDN", response);
}

ModemStatus xE310::reboot() {
    AtResponse response;
    return controller_.send_raw("AT#REBOOT", response);
}

// --- Network Registration ---

static SurvCell parse_surv_line(const std::string& line) {
    // Try to determine type by field count.
    // 4G: 9 comma-separated fields
    // 2G BCCH: 10 fields
    // 2G non-BCCH: 2 fields
    SurvCell cell;
    std::vector<std::string> fields;
    std::string_view sv = line;
    while (!sv.empty()) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        fields.push_back(std::string(token));
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (fields.size() == 2) {
        // 2G non-BCCH: <arfcn>,<rxLev>
        cell.type   = SurvCellType::cell_2g_non_bcch;
        cell.arfcn  = std::atoi(fields[0].c_str());
        cell.rx_lev = std::atoi(fields[1].c_str());
    } else if (fields.size() == 7) {
        // LTE (AT#CSURVC): <earfcn>,<rsrp>,<mcc>,<mnc>,<pci>,<tac>,<cell_id>
        cell.type          = SurvCellType::cell_4g;
        cell.earfcn        = std::atoi(fields[0].c_str());
        cell.rsrp          = std::strtof(fields[1].c_str(), nullptr);
        cell.rx_lev        = static_cast<int>(cell.rsrp);
        cell.mcc           = static_cast<uint16_t>(std::atoi(fields[2].c_str()));
        cell.mnc           = static_cast<uint16_t>(std::atoi(fields[3].c_str()));
        cell.phys_cell_id  = static_cast<uint32_t>(std::atoi(fields[4].c_str()));
        cell.tac           = static_cast<uint32_t>(std::atoi(fields[5].c_str()));
        cell.cell_identity = static_cast<uint64_t>(std::atoll(fields[6].c_str()));
    } else if (fields.size() >= 10) {
        // 2G BCCH: <arfcn>,<bsic>,<rxLev>,<ber>,<mcc>,<mnc>,<lac>,<cellId>,<cellStat>,<numArfcn>
        cell.type     = SurvCellType::cell_2g_bcch;
        cell.arfcn    = std::atoi(fields[0].c_str());
        cell.bsic     = std::atoi(fields[1].c_str());
        cell.rx_lev   = std::atoi(fields[2].c_str());
        cell.ber      = std::atoi(fields[3].c_str());
        cell.mcc      = static_cast<uint16_t>(std::strtoul(fields[4].c_str(), nullptr, 16));
        cell.mnc      = static_cast<uint16_t>(std::strtoul(fields[5].c_str(), nullptr, 16));
        cell.lac      = static_cast<uint32_t>(std::strtoul(fields[6].c_str(), nullptr, 0));
        cell.cell_id  = static_cast<uint32_t>(std::strtoul(fields[7].c_str(), nullptr, 0));
        cell.cell_stat = fields[8];
        cell.num_arfcn = std::atoi(fields[9].c_str());
    } else if (fields.size() >= 9) {
        // 4G: <earfcn>,<rxLev>,<mcc>,<mnc>,<cellId>,<tac>,<cellIdentity>,<rsrp>,<rsrq>
        cell.type         = SurvCellType::cell_4g;
        cell.earfcn       = std::atoi(fields[0].c_str());
        cell.rx_lev       = std::atoi(fields[1].c_str());
        cell.mcc          = static_cast<uint16_t>(std::strtoul(fields[2].c_str(), nullptr, 16));
        cell.mnc          = static_cast<uint16_t>(std::strtoul(fields[3].c_str(), nullptr, 16));
        cell.phys_cell_id = static_cast<uint32_t>(std::strtoul(fields[4].c_str(), nullptr, 16));
        cell.tac          = static_cast<uint32_t>(std::strtoul(fields[5].c_str(), nullptr, 16));
        cell.cell_identity = static_cast<uint64_t>(std::strtoull(fields[6].c_str(), nullptr, 16));
        cell.rsrp         = std::strtof(fields[7].c_str(), nullptr);
        cell.rsrq         = std::strtof(fields[8].c_str(), nullptr);
    }
    return cell;
}

ModemStatus xE310::network_survey(NetworkSurveyResult& result,
                                   uint32_t start_ch, uint32_t end_ch) {
    AtResponse response;
    std::string cmd;
    if (start_ch == 0 && end_ch == 0) {
        cmd = "AT#CSURVC";
    } else {
        cmd = "AT#CSURVC=" + std::to_string(start_ch) + "," + std::to_string(end_ch);
    }
    auto status = controller_.send_raw(cmd, response,120000); // network survey can take a long time, allow up to 60s
    if (status != ModemStatus::ok) {
        return status;
    }

    result = {};

    // Split body on AT_TERMINATOR and parse each line
    std::string_view body     = response.body;
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end  = body.find(terminator, start);
        auto line = (end == std::string_view::npos)
                    ? std::string(body.substr(start))
                    : std::string(body.substr(start, end - start));
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        if (line.empty() || line.rfind("Network survey started", 0) == 0) {
            continue;
        }

        // "Network survey ended" — with or without "(Carrier: N BCCh: M)" suffix
        if (line.rfind("Network survey ended", 0) == 0) {
            result.has_summary = true;
            auto carrier_pos = line.find("Carrier:");
            auto bcch_pos = line.find("BCCh:");
            if (carrier_pos != std::string::npos && bcch_pos != std::string::npos) {
                const char* carrier_ptr = line.c_str() + carrier_pos + std::strlen("Carrier:");
                const char* bcch_ptr = line.c_str() + bcch_pos + std::strlen("BCCh:");
                while (*carrier_ptr == ' ') ++carrier_ptr;
                while (*bcch_ptr == ' ') ++bcch_ptr;
                result.no_arfcn = std::atoi(carrier_ptr);
                result.no_bcch = std::atoi(bcch_ptr);
            }
            continue;
        }

        result.cells.push_back(parse_surv_line(line));
    }

    return ModemStatus::ok;
}

// needs reboot to take effect, so we don't apply it directly in the attach flow, but expose it for manual configuration/testing
ModemStatus xE310::set_iot_tech(RadioTech tech, uint8_t gsm_priority) {
    AtResponse response;
    uint8_t n = 0;
    #ifdef ME310M1
        if(tech == RadioTech::cat_m1)
            n = 0;
        else if(tech == RadioTech::nb_iot)
            n = 1;
        else
            return ModemStatus::invalid_param;
        gsm_priority = 0; // GSM is not supported on ME310M1, so priority param is ignored and set to 0
    #endif
    
    auto cmd = "AT#WS46=" + std::to_string(n) + "," + std::to_string(gsm_priority);
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_iot_tech(RadioTech& tech, uint8_t& gsm_priority) {
    AtResponse response;
    auto status = controller_.send_raw("AT#WS46?", response,5000);
    if (status == ModemStatus::ok) {
        // Response body: "#WS46: <n>,<GSM_P>"
        auto fields = parse_at_params(response.body);
        if (fields.size() >= 2) {
            unsigned int nv = static_cast<unsigned int>(std::strtoul(fields[0].c_str(), nullptr, 10));
            unsigned int gp = static_cast<unsigned int>(std::strtoul(fields[1].c_str(), nullptr, 10));
            //#ifdef ME301M1
            if(nv == 0)
                tech = RadioTech::cat_m1;
            else if(nv == 1)
                tech = RadioTech::nb_iot;
            else
                tech = RadioTech::unknown;
            gsm_priority = static_cast<uint8_t>(gp);
            //#else
            //tech         = static_cast<RadioTech>(nv);
            //gsm_priority = static_cast<uint8_t>(gp);
            //#endif
        }
    }
    return status;
}

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

ModemStatus xE310::get_bands(BandConfig& bands) {
    AtResponse response;
    // Response: #BND: <band>,<UMTS_band>,<LTE_band>,<TDSCDMA_band>,<LTE_band_over_64>
    auto status = controller_.send_raw("AT#BND?", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    auto fields = parse_at_params(response.body);
    if (fields.size() < 5) {
        return ModemStatus::at_error;
    }

    auto parse_u64 = [](const std::string& s, uint64_t& out) -> bool {
        char* end = nullptr;
        unsigned long long v = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0') return false;
        out = static_cast<uint64_t>(v);
        return true;
    };

    BandConfig parsed;
    if (!parse_u64(fields[0], parsed.gsm_mask) ||
        !parse_u64(fields[1], parsed.umts_mask) ||
        !parse_u64(fields[2], parsed.lte_mask) ||
        !parse_u64(fields[3], parsed.tdscdma_mask) ||
        !parse_u64(fields[4], parsed.lte_mask_over_64)) {
        return ModemStatus::at_error;
    }

    bands = parsed;
    return ModemStatus::ok;
}

ModemStatus xE310::set_registration_urc(bool enable) {
    AtResponse response;
    auto result = controller_.send_raw(std::string("AT+CEREG=") + (enable ? "4" : "0"), response);
    if (result != ModemStatus::ok) {
        return result;
    }
    /*
    result = controller_.send_raw(std::string("AT+CEREG=") + (enable ? "2" : "0"), response);
    if (result != ModemStatus::ok) {
        return result;
    }
    */

    return ModemStatus::ok;
}

ModemStatus xE310::delete_mru_list(MruListRat rat) {
    AtResponse response;
    auto cmd = "AT%TRSHCMD=\"BSPFILE\",\"ERASE_LTEPP\"," +
               std::to_string(static_cast<unsigned int>(rat));
    return controller_.send_raw(cmd, response);
}

ModemStatus xE310::get_registration_status(RegistrationInfo& info, RadioTech tech) {
    AtResponse response;
    std::string cmd = (tech == RadioTech::gsm) ? "AT+CREG?" : "AT+CEREG?";
    auto result = controller_.send_raw(cmd, response,5000);
    if (result != ModemStatus::ok) {
        return result;
    }

    // Response body: "+CEREG: <mode>,<stat>[,<lac>,<ci>[,<AcT>]]"
    auto fields = parse_at_params(response.body);
    if (fields.size() < 2) {
        return ModemStatus::at_error;
    }

    info.mode = static_cast<uint8_t>(std::atoi(fields[0].c_str()));
    info.stat = static_cast<RegStatus>(std::atoi(fields[1].c_str()));

    if (fields.size() >= 4) {
        info.lac = fields[2];
        info.ci  = fields[3];
        info.has_location = true;
    }

    if (fields.size() >= 5) {
        info.act = static_cast<RadioTech>(std::atoi(fields[4].c_str()));
    }

    return ModemStatus::ok;
}

ModemStatus xE310::network_attach() {
    AtResponse response;
    return controller_.send_raw("AT+CGATT=1", response, 30000);
}

ModemStatus xE310::network_detach() {
    AtResponse response;
    return controller_.send_raw("AT+CGATT=0", response, 30000);
}

ModemStatus xE310::get_signal_quality(SignalQuality& sq) {
    AtResponse response;
    auto status = controller_.send_raw("AT+CESQ", response);
    if (status == ModemStatus::ok) {
        // Response: +CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>
        // rscp and ecno are WCDMA-only — ignored here.
        auto fields = parse_at_params(response.body);
        if (fields.size() >= 6) {
            sq.rssi = std::atoi(fields[0].c_str());
            sq.ber  = std::atoi(fields[1].c_str());
            sq.rsrq = std::atoi(fields[4].c_str());
            sq.rsrp = std::atoi(fields[5].c_str());
        }
    }
    return status;
}

ModemStatus xE310::set_radio_tech(RadioTech tech) {
    AtResponse response;
    // AT+COPS=0,,,<act> — automatic selection with specific access technology
    return controller_.send_raw("AT+COPS=0,,," + std::to_string(static_cast<int>(tech)), response,45000); // registration can take a long time, allow up to 30s
}

ModemStatus xE310::set_operator_manual(const std::string& oper, RadioTech tech) {
    AtResponse response;
    // AT+COPS=1,2,"oper",<act> — manual selection, numeric format
    // Mode 4 (manual with auto fallback) is not supported by xE310; use mode 1.
    if(oper.empty()) {
        return set_operator_auto();
    } else {
        auto cmd = "AT+COPS=1,2,\"" + oper + "\"," + std::to_string(static_cast<int>(tech));

        return controller_.send_raw(cmd, response, 210000); // manual registration can take a long time, allow up to 30s
    }
}

ModemStatus xE310::set_operator_auto() {
    AtResponse response;
    return controller_.send_raw("AT+COPS=0", response,210000); // registration can take a long time, allow up to 30s 
}

ModemStatus xE310::get_operator(std::string& oper) {
    AtResponse response;
    auto status = controller_.send_raw("AT+COPS?", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Response body: "+COPS: <mode>[,<format>,<oper>,<act>]"
    // If deregistered only <mode> is present — oper is left unchanged.
    auto fields = parse_at_params(response.body);
    if (fields.empty()) {
        return ModemStatus::at_error;
    }

    // fields[2] = <oper> (quoted string, already unquoted by parse_at_params)
    if (fields.size() >= 3) {
        oper = fields[2];
    }

    return ModemStatus::ok;
}

ModemStatus xE310::get_available_operators(std::vector<Operator>& operators) {
    AtResponse response;
    // AT+COPS=? can take up to 3 minutes to complete a full scan
    auto status = controller_.send_raw("AT+COPS=?", response, 210000);
    if (status != ModemStatus::ok) {
        return status;
    }

    operators.clear();

    // Response body example:
    // +COPS: (2,"Vodafone PT","Vodafone","26801",8),(1,"NOS","NOS","26803",8),...
    // Each operator entry is wrapped in parentheses.
    const std::string& body = response.body;
    size_t pos = 0;
    while (pos < body.size()) {
        auto open = body.find('(', pos);
        if (open == std::string::npos) break;
        auto close = body.find(')', open);
        if (close == std::string::npos) break;

        std::string entry = body.substr(open + 1, close - open - 1);
        pos = close + 1;

        // Tokenize comma-separated fields, respecting quoted strings
        std::vector<std::string> fields;
        size_t fp = 0;
        while (fp <= entry.size()) {
            // skip whitespace
            while (fp < entry.size() && entry[fp] == ' ') ++fp;
            if (fp >= entry.size()) break;

            size_t start = fp;
            if (entry[fp] == '"') {
                // quoted field
                ++fp;
                while (fp < entry.size() && entry[fp] != '"') ++fp;
                if (fp < entry.size()) ++fp; // skip closing quote
                // advance to comma or end
                while (fp < entry.size() && entry[fp] != ',') ++fp;
                fields.push_back(entry.substr(start, fp - start));
            } else {
                while (fp < entry.size() && entry[fp] != ',') ++fp;
                fields.push_back(entry.substr(start, fp - start));
            }
            if (fp < entry.size() && entry[fp] == ',') ++fp;
        }

        if (fields.size() < 4) continue; // need at least stat + 3 name fields

        auto strip = [](const std::string& s) -> std::string {
            auto a = s.find_first_not_of(" \t\"");
            auto b = s.find_last_not_of(" \t\"");
            return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };

        Operator op;
        // fields[0] = stat (skip), [1] = long_name, [2] = short_name, [3] = numeric, [4] = act
        op.long_name  = strip(fields[1]);
        op.short_name = strip(fields[2]);
        op.numeric    = strip(fields[3]);
        op.act        = (fields.size() >= 5)
                        ? static_cast<RadioTech>(std::atoi(fields[4].c_str()))
                        : RadioTech::gsm;
        operators.push_back(std::move(op));
    }

    return ModemStatus::ok;
}

ModemStatus xE310::scan_networks(CsurvResult& result, uint32_t start_ch, uint32_t end_ch) {
    AtResponse response;

    // Set numeric output format and enable summary line (Carrier/BCCh counts).
    auto status = controller_.send_raw("AT#CSURVF=2", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Build survey command (optional channel range).
    std::string cmd;
    if (start_ch == 0 && end_ch == 0) {
        cmd = "AT#CSURV";
    } else {
        cmd = "AT#CSURV=" + std::to_string(start_ch) + "," + std::to_string(end_ch);
    }
    status = controller_.send_raw(cmd, response, 120000);
    if (status != ModemStatus::ok) {
        return status;
    }

    result = {};

    // Extract the string value that follows a labeled key in a space-delimited line.
    // e.g. extract_value("earfcn: 1234 rxLev: -80", "earfcn:") → "1234"
    auto extract_value = [](const std::string& line, const char* label) -> std::string {
        auto pos = line.find(label);
        if (pos == std::string::npos) return "";
        pos += std::strlen(label);
        while (pos < line.size() && line[pos] == ' ') ++pos;
        auto end = line.find(' ', pos);
        return (end == std::string::npos) ? line.substr(pos) : line.substr(pos, end - pos);
    };

    // Split body on AT_TERMINATOR and parse each line.
    std::string_view body       = response.body;
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end  = body.find(terminator, start);
        auto line = (end == std::string_view::npos)
                    ? std::string(body.substr(start))
                    : std::string(body.substr(start, end - start));
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        if (line.empty() || line.rfind("Network survey started", 0) == 0) {
            continue;
        }

        // Summary line: "Network survey ended (Carrier: <N> BCCh: <M>)"
        if (line.rfind("Network survey ended", 0) == 0) {
            result.has_summary = true;
            auto carrier_pos = line.find("Carrier:");
            auto bcch_pos = line.find("BCCh:");
            if (carrier_pos != std::string::npos && bcch_pos != std::string::npos) {
                const char* carrier_ptr = line.c_str() + carrier_pos + std::strlen("Carrier:");
                const char* bcch_ptr = line.c_str() + bcch_pos + std::strlen("BCCh:");
                while (*carrier_ptr == ' ') ++carrier_ptr;
                while (*bcch_ptr == ' ') ++bcch_ptr;
                result.no_arfcn = std::atoi(carrier_ptr);
                result.no_bcch = std::atoi(bcch_ptr);
            }
            continue;
        }

        // Cell line format (CSURVF=2, labeled, hex numerics):
        // earfcn: <earfcn> rxLev: <rxLev> mcc: <mcc> mnc: <mnc>
        //   cellid: <cellId> tac: <tac> cellIdentity: <cellIdentity>
        auto earfcn_str = extract_value(line, "earfcn:");
        if (earfcn_str.empty()) continue;

        CsurvCell cell;
        cell.earfcn        = std::atoi(earfcn_str.c_str());
        cell.rx_lev        = std::atoi(extract_value(line, "rxLev:").c_str());
        cell.mcc           = static_cast<uint16_t>(std::strtoul(extract_value(line, "mcc:").c_str(),         nullptr, 16));
        cell.mnc           = static_cast<uint16_t>(std::strtoul(extract_value(line, "mnc:").c_str(),         nullptr, 16));
        cell.cell_id       = static_cast<uint32_t>(std::strtoul(extract_value(line, "cellid:").c_str(),      nullptr, 16));
        cell.tac           = static_cast<uint32_t>(std::strtoul(extract_value(line, "tac:").c_str(),         nullptr, 16));
        cell.cell_identity = static_cast<uint64_t>(std::strtoull(extract_value(line, "cellIdentity:").c_str(), nullptr, 16));

        result.cells.push_back(std::move(cell));
    }

    return ModemStatus::ok;
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
    if (status != ModemStatus::ok) {
        return status;
    }

    // Response body may contain multiple lines (one per context), joined by AT_TERMINATOR.
    // Each line: +CGDCONT: <cid>,<PDP_type>,<APN>,<PDP_addr>,<d_comp>,<h_comp>,0,0
    std::string_view body = response.body;
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end  = body.find(terminator, start);
        auto line = (end == std::string_view::npos)
                    ? std::string(body.substr(start))
                    : std::string(body.substr(start, end - start));
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        // Find the prefix "+CGDCONT:"
        constexpr std::string_view prefix = "+CGDCONT:";
        auto p = line.find(prefix);
        if (p == std::string::npos) continue;

        // Tokenize comma-separated fields after the colon
        std::string params = line.substr(p + prefix.size());
        std::vector<std::string> fields;
        size_t pos = 0;
        while (pos <= params.size()) {
            auto comma = params.find(',', pos);
            if (comma == std::string::npos) {
                fields.push_back(params.substr(pos));
                break;
            }
            fields.push_back(params.substr(pos, comma - pos));
            pos = comma + 1;
        }

        auto strip = [](const std::string& s) -> std::string {
            auto a = s.find_first_not_of(" \t\"");
            auto b = s.find_last_not_of(" \t\"");
            return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };

        if (fields.empty()) continue;

        // fields[0] = cid
        uint8_t line_cid = static_cast<uint8_t>(std::atoi(strip(fields[0]).c_str()));
        if (line_cid != cid) continue;

        // fields[2] = APN
        if (fields.size() >= 3) apn = strip(fields[2]);
        // fields[3] = PDP_addr (IP address assigned by network)
        //if (fields.size() >= 4) regInfo.ip_address = strip(fields[3]);

        return ModemStatus::ok;
    }

    // Context with requested cid not found in response
    return ModemStatus::at_error;
}

ModemStatus xE310::set_pdp_urc(bool enable) {
    /* discard unsolicited result codes when TA-TE link is
    reserved (e.g. in on-line data mode); otherwise forward
    them directly to the TE.
    */
    AtResponse response;
    auto result = controller_.send_raw(std::string("AT+CGEREP=") + (enable ? "1" : "0"), response);
    if (result != ModemStatus::ok) {
        return result;
    }

    return ModemStatus::ok;
}

ModemStatus xE310::activate_pdp(uint8_t cid) {
    AtResponse response;
    return controller_.send_raw("AT#SGACT=1," + std::to_string(cid), response);
}

ModemStatus xE310::deactivate_pdp(uint8_t cid) {
    AtResponse response;
    return controller_.send_raw("AT#SGACT=0," + std::to_string(cid), response);
}

ModemStatus xE310::get_pdp_state(uint8_t cid, bool& active) {
    AtResponse response;
    auto status = controller_.send_raw("AT#SGACT?", response);
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
                             uint16_t local_port) {
    if(local_port == 0)
        local_port = remote_port; // If local port is not specified, use the same as remote port
    AtResponse response;
    // AT#SD=<connId>,<txProt>,<rPort>,"<IPaddr>",<closureType>,<lPort>,<connMode>
    // txProt=1 (UDP), closureType=0 (not applicable for UDP), connMode=1 (command mode)
    auto cmd = "AT#SD=" + std::to_string(conn_id) + ",1,"
             + std::to_string(remote_port) + ",\""
             + host + "\",0,"
             + std::to_string(local_port) + ",1" + ",0" + ",1";
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
        auto fields = parse_at_params(response.body);
        if (fields.size() >= 2) {
            state = static_cast<uint8_t>(std::atoi(fields[1].c_str()));
        }
    }
    return status;
}

ModemStatus xE310::send_at_command(const std::string& command, std::string& response, uint32_t timeout_ms) {
    AtResponse at_response;
    auto status = controller_.send_raw(command, at_response, timeout_ms);
    if (status == ModemStatus::ok) {
        response = at_response.body;
    }
    return status;
}

// --- Power Options ---

// --- Event Handlers ---
const RegistrationInfo& xE310::registration_info() const { return info; }

std::vector<std::string> xE310::poll_urc(uint32_t timeout_ms) {
    return controller_.poll_urc(timeout_ms);
}

} // namespace modem
