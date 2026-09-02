#include "modem/xe310.h"
#include "modem/at_command.h"
#include "hal/log.h"
#include "hal/timer_factory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace modem {

xE310::xE310(ModemController& controller)
    : controller_(controller) {}

ModemStatus xE310::last_status() const {
    return last_status_;
}

ModemStatus xE310::send_raw(std::string_view command, AtResponse& response, uint32_t timeout_ms, bool retry) {
    last_status_ = controller_.send_raw(command, response, timeout_ms, retry);
    return last_status_;
}

ModemStatus xE310::set_baudrate(uint32_t baudrate) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+IPR=%u", static_cast<unsigned>(baudrate));
    return send_raw(cmd, response);
}

ModemStatus xE310::set_echo(bool enable) {
    AtResponse response;
    return send_raw(enable ? "ATE1" : "ATE0", response);
}

ModemStatus xE310::at_ok() {
    AtResponse response;
    return send_raw("AT", response);
}

ModemStatus xE310::request_imei_sv(FixedString<MODEM_SHORT_STR>& imei_sv) {
    AtResponse response;
    auto status = send_raw("AT+IMEISV", response);
    if (status == ModemStatus::ok) {
        imei_sv = response.body.view();
    }
    return status;
}

ModemStatus xE310::request_model_id(FixedString<MODEM_MEDIUM_STR>& model) {
    AtResponse response;
    auto status = send_raw("AT#CGMM", response);
    if (status == ModemStatus::ok) {
        constexpr std::string_view PREFIX = "#CGMM: ";
        if (response.body.rfind(PREFIX, 0) == 0) {
            model = response.body.substr(PREFIX.size());
        } else {
            model = response.body;
        }
    }
    return status;
}

ModemStatus xE310::request_sw_package_version(SoftwarePackageVersion& ver) {
    AtResponse response;
    auto status = send_raw("AT#SWPKGV", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Body contains 4 lines joined by AT_TERMINATOR
    std::string_view body = response.body.view();
    std::string_view terminator = AT_TERMINATOR;
    // Fields in order
    std::string_view::size_type start = 0;
    int field_idx = 0;
    while (start < body.size() && field_idx < 4) {
        auto end = body.find(terminator, start);
        std::string_view field_val =
            (end == std::string_view::npos) ? body.substr(start) : body.substr(start, end - start);
        switch (field_idx) {
            case 0: ver.package_version = field_val; break;
            case 1: ver.modem_version = field_val; break;
            case 2: ver.prod_params_version = field_val; break;
            case 3: ver.app_version = field_val; break;
            default: break;
        }
        if (end == std::string_view::npos) break;
        start = end + terminator.size();
        ++field_idx;
    }

    return ModemStatus::ok;
}

ModemStatus xE310::request_telit_id(FixedString<MODEM_MEDIUM_STR>& tid) {
    AtResponse response;
    auto status = send_raw("AT#TID", response);
    if (status == ModemStatus::ok) {
        tid = response.body.view();
    }
    return status;
}

ModemStatus xE310::request_identification(FixedString<MODEM_LONG_STR>& info) {
    AtResponse response;
    auto status = send_raw("ATI", response);
    if (status == ModemStatus::ok) {
        info = response.body.view();
    }
    return status;
}

ModemStatus xE310::get_imei(FixedString<MODEM_SHORT_STR>& imei) {
    AtResponse response;
    auto status = send_raw("AT+CGSN", response);
    if (status == ModemStatus::ok) {
        imei = response.body.view();
    }
    return status;
}

ModemStatus xE310::get_clock(FixedString<MODEM_SHORT_STR>& clock) {
    AtResponse response;
    auto status = send_raw("AT+CCLK?", response);
    if (status == ModemStatus::ok) {
        constexpr std::string_view PREFIX = "+CCLK: ";
        if (response.body.rfind(PREFIX, 0) == 0) {
            std::string_view value = response.body.substr(PREFIX.size());
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            clock = value;
        } else {
            clock = response.body.view();
        }
    }
    return status;
}

// --- SIM Card ---

ModemStatus xE310::read_iccid(FixedString<MODEM_SHORT_STR>& iccid) {
    AtResponse response;
    auto status = send_raw("AT#CCID", response);
    if (status == ModemStatus::ok) {
        constexpr std::string_view PREFIX = "#CCID: ";
        if (response.body.rfind(PREFIX, 0) == 0) {
            iccid = response.body.substr(PREFIX.size());
        } else {
            iccid = response.body;
        }
    }
    return status;
}

ModemStatus xE310::read_imsi(FixedString<MODEM_SHORT_STR>& imsi) {
    AtResponse response;
    auto status = send_raw("AT+CIMI", response);
    if (status == ModemStatus::ok) {
        imsi = response.body.view();
    }
    return status;
}

ModemStatus xE310::set_sim_detection(SimDetMode mode) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SIMDET=%d", static_cast<int>(mode));
    return send_raw(cmd, response);
}

ModemStatus xE310::query_sim_status(SimStatus& status) {
    AtResponse response;
    auto result = send_raw("AT#QSS?", response);
    if (result == ModemStatus::ok) {
        // Response body: "#QSS: <mode>,<status>"
        auto comma_pos = response.body.find(',');
        if (comma_pos != FixedString<AT_RESPONSE_MAX>::NPOS) {
            status = static_cast<SimStatus>(std::atoi(response.body.c_str() + comma_pos + 1));
        }
    }
    return result;
}

ModemStatus xE310::send_sim_command(std::string_view command, FixedString<AT_RESPONSE_MAX>& sim_response) {
    AtResponse response;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+CSIM=%u,\"%.*s\"", static_cast<unsigned>(command.size()), (int)command.size(),
             command.data());
    auto status = send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        sim_response = response.body.view();
    }
    return status;
}

// --- PSM ---

ModemStatus xE310::set_psm(const CpsmsConfig& cfg) {
    AtResponse response;
    char cmd[256];
    int pos = snprintf(cmd, sizeof(cmd), "AT+CPSMS=%d", static_cast<int>(cfg.mode));
    if (!cfg.req_periodic_rau.empty() || !cfg.req_gprs_ready_timer.empty() || !cfg.req_periodic_tau.empty() ||
        !cfg.req_active_time.empty()) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, ",");
        if (!cfg.req_periodic_rau.empty())
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, "\"%s\"", cfg.req_periodic_rau.c_str());
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, ",");
        if (!cfg.req_gprs_ready_timer.empty())
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, "\"%s\"", cfg.req_gprs_ready_timer.c_str());
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, ",");
        if (!cfg.req_periodic_tau.empty())
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, "\"%s\"", cfg.req_periodic_tau.c_str());
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, ",");
        if (!cfg.req_active_time.empty()) snprintf(cmd + pos, sizeof(cmd) - pos, "\"%s\"", cfg.req_active_time.c_str());
    }
    return send_raw(cmd, response);
}

ModemStatus xE310::get_psm(CpsmsConfig& cfg) {
    AtResponse response;
    auto status = send_raw("AT+CPSMS?", response);
    if (status != ModemStatus::ok) {
        return status;
    }
    // Response body: "+CPSMS: <mode>,[<RAU>],[<GPRSTimer>],[<TAU>],[<ActiveTime>]"
    std::string_view body = response.body.view();
    auto colon = body.find(':');
    if (colon == std::string_view::npos) {
        return ModemStatus::at_error;
    }
    std::string_view params = body.substr(colon + 1);

    auto strip_quotes = [](std::string_view s) -> std::string_view {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    };

    // Tokenize on commas
    FixedString<MODEM_SHORT_STR> fields[5];
    int field_count = 0;
    std::string_view sv = params;
    while (!sv.empty() && field_count < 5) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        auto start = token.find_first_not_of(' ');
        fields[field_count] = (start == std::string_view::npos) ? std::string_view("") : token.substr(start);
        ++field_count;
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (field_count >= 1) cfg.mode = static_cast<PsmMode>(std::atoi(fields[0].c_str()));
    if (field_count >= 2) cfg.req_periodic_rau = strip_quotes(fields[1].view());
    if (field_count >= 3) cfg.req_gprs_ready_timer = strip_quotes(fields[2].view());
    if (field_count >= 4) cfg.req_periodic_tau = strip_quotes(fields[3].view());
    if (field_count >= 5) cfg.req_active_time = strip_quotes(fields[4].view());

    return ModemStatus::ok;
}

ModemStatus xE310::disable_psm() {
    AtResponse response;
    return send_raw("AT+CPSMS=0", response);
}

ModemStatus xE310::set_telit_psm(const TelitCpsmsConfig& cfg) {
    AtResponse response;
    char cmd[256];
    int pos = snprintf(cmd, sizeof(cmd), "AT#CPSMS=%d", static_cast<int>(cfg.mode));

    // Determine last field that has a value
    int last = 0;
    if (cfg.fHasPeriodicRau) last = 1;
    if (cfg.fHasGprsReadyTimer) last = 2;
    if (cfg.fHasPeriodicTau) last = 3;
    if (cfg.fHasActiveTime) last = 4;
    if (cfg.fHasPsmVersion) last = 5;
    if (cfg.fHasPsmThreshold) last = 6;

    auto append_opt = [&](int field, bool has, uint32_t val) {
        if (field <= last) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, ",");
            if (has) pos += snprintf(cmd + pos, sizeof(cmd) - pos, "%u", static_cast<unsigned>(val));
        }
    };

    append_opt(1, cfg.fHasPeriodicRau, cfg.req_periodic_rau);
    append_opt(2, cfg.fHasGprsReadyTimer, cfg.req_gprs_ready_timer);
    append_opt(3, cfg.fHasPeriodicTau, cfg.req_periodic_tau);
    append_opt(4, cfg.fHasActiveTime, cfg.req_active_time);
    append_opt(5, cfg.fHasPsmVersion, static_cast<uint32_t>(cfg.psm_version));
    append_opt(6, cfg.fHasPsmThreshold, cfg.psm_threshold);

    return send_raw(cmd, response);
}

ModemStatus xE310::get_telit_psm(TelitCpsmsStatus& st) {
    AtResponse response;
    auto result = send_raw("AT#CPSMS?", response);
    if (result != ModemStatus::ok) {
        return result;
    }
    // Response body: "#CPSMS: <status>,[<T3324>],[<T3412>],<psmVersion>,<psmThreshold>,<mode>"
    std::string_view body = response.body.view();
    auto colon = body.find(':');
    if (colon == std::string_view::npos) {
        return ModemStatus::at_error;
    }
    std::string_view params = body.substr(colon + 1);

    // Parse up to 6 comma-separated fields
    char field_buf[6][32];
    int field_count = 0;
    std::string_view sv = params;
    while (!sv.empty() && field_count < 6) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        auto start = token.find_first_not_of(' ');
        if (start == std::string_view::npos) {
            field_buf[field_count][0] = '\0';
        } else {
            auto trimmed = token.substr(start);
            size_t len = std::min(trimmed.size(), sizeof(field_buf[0]) - 1);
            std::memcpy(field_buf[field_count], trimmed.data(), len);
            field_buf[field_count][len] = '\0';
        }
        ++field_count;
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (field_count >= 1) st.status = static_cast<uint8_t>(std::atoi(field_buf[0]));
    if (field_count >= 2 && field_buf[1][0] != '\0') st.t3324 = static_cast<uint32_t>(std::atol(field_buf[1]));
    if (field_count >= 3 && field_buf[2][0] != '\0') st.t3412 = static_cast<uint32_t>(std::atol(field_buf[2]));
    if (field_count >= 4) st.psm_version = static_cast<uint8_t>(std::atoi(field_buf[3]));
    if (field_count >= 5) st.psm_threshold = static_cast<uint32_t>(std::atol(field_buf[4]));
    if (field_count >= 6) st.mode = static_cast<PsmMode>(std::atoi(field_buf[5]));

    return ModemStatus::ok;
}

ModemStatus xE310::disable_telit_psm() {
    AtResponse response;
    return send_raw("AT#CPSMS=0", response);
}

ModemStatus xE310::set_psm_urc(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT#PSMURC=1" : "AT#PSMURC=0", response);
}

ModemStatus xE310::get_psm_urc(bool& enabled) {
    AtResponse response;
    auto status = send_raw("AT#PSMURC?", response);
    if (status == ModemStatus::ok) {
        // Response body: "#PSMURC: <en>"
        auto colon = response.body.find(':');
        if (colon != FixedString<AT_RESPONSE_MAX>::NPOS) {
            enabled = std::atoi(response.body.c_str() + colon + 1) != 0;
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
    return send_raw("AT+CFUN=1", response, 15000);
}

ModemStatus xE310::power_off_radio() {
    AtResponse response;
    return send_raw("AT+CFUN=0", response, 15000);
}

ModemStatus xE310::shutdown() {
    AtResponse response;
    // go to DH0 mode
    send_raw("AT+CFUN=4", response, 15000);
    return send_raw("AT+CFUN=11", response, 15000); // use reset button on devkit to wake up modem
    // return send_raw("AT#SHDN", response); // use on/off
}

ModemStatus xE310::reboot() {
    AtResponse response;
    auto status = send_raw("AT#REBOOT", response);
    delay_ms(2000);
    return status;
}

// --- Network Registration ---

static SurvCell parse_surv_line(std::string_view line) {
    SurvCell cell;

    // Tokenize comma-separated fields into stack array
    char field_buf[10][64];
    int field_count = 0;
    std::string_view sv = line;
    while (!sv.empty() && field_count < 10) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        size_t len = std::min(token.size(), sizeof(field_buf[0]) - 1);
        std::memcpy(field_buf[field_count], token.data(), len);
        field_buf[field_count][len] = '\0';
        ++field_count;
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (field_count == 2) {
        // 2G non-BCCH: <arfcn>,<rxLev>
        cell.type = SurvCellType::cell_2g_non_bcch;
        cell.arfcn = std::atoi(field_buf[0]);
        cell.rx_lev = std::atoi(field_buf[1]);
    } else if (field_count == 7) {
        // LTE (AT#CSURVC): <earfcn>,<rsrp>,<mcc>,<mnc>,<pci>,<tac>,<cell_id>
        cell.type = SurvCellType::cell_4g;
        cell.earfcn = std::atoi(field_buf[0]);
        cell.rsrp = std::strtof(field_buf[1], nullptr);
        cell.rx_lev = static_cast<int>(cell.rsrp);
        cell.mcc = static_cast<uint16_t>(std::atoi(field_buf[2]));
        cell.mnc = static_cast<uint16_t>(std::atoi(field_buf[3]));
        cell.phys_cell_id = static_cast<uint32_t>(std::atoi(field_buf[4]));
        cell.tac = static_cast<uint32_t>(std::atoi(field_buf[5]));
        cell.cell_identity = static_cast<uint64_t>(std::atoll(field_buf[6]));
    } else if (field_count >= 10) {
        // 2G BCCH: <arfcn>,<bsic>,<rxLev>,<ber>,<mcc>,<mnc>,<lac>,<cellId>,<cellStat>,<numArfcn>
        cell.type = SurvCellType::cell_2g_bcch;
        cell.arfcn = std::atoi(field_buf[0]);
        cell.bsic = std::atoi(field_buf[1]);
        cell.rx_lev = std::atoi(field_buf[2]);
        cell.ber = std::atoi(field_buf[3]);
        cell.mcc = static_cast<uint16_t>(std::strtoul(field_buf[4], nullptr, 16));
        cell.mnc = static_cast<uint16_t>(std::strtoul(field_buf[5], nullptr, 16));
        cell.lac = static_cast<uint32_t>(std::strtoul(field_buf[6], nullptr, 0));
        cell.cell_id = static_cast<uint32_t>(std::strtoul(field_buf[7], nullptr, 0));
        cell.cell_stat = std::string_view(field_buf[8]);
        cell.num_arfcn = std::atoi(field_buf[9]);
    } else if (field_count >= 9) {
        // 4G: <earfcn>,<rxLev>,<mcc>,<mnc>,<cellId>,<tac>,<cellIdentity>,<rsrp>,<rsrq>
        cell.type = SurvCellType::cell_4g;
        cell.earfcn = std::atoi(field_buf[0]);
        cell.rx_lev = std::atoi(field_buf[1]);
        cell.mcc = static_cast<uint16_t>(std::strtoul(field_buf[2], nullptr, 16));
        cell.mnc = static_cast<uint16_t>(std::strtoul(field_buf[3], nullptr, 16));
        cell.phys_cell_id = static_cast<uint32_t>(std::strtoul(field_buf[4], nullptr, 16));
        cell.tac = static_cast<uint32_t>(std::strtoul(field_buf[5], nullptr, 16));
        cell.cell_identity = static_cast<uint64_t>(std::strtoull(field_buf[6], nullptr, 16));
        cell.rsrp = std::strtof(field_buf[7], nullptr);
        cell.rsrq = std::strtof(field_buf[8], nullptr);
    }
    return cell;
}

ModemStatus xE310::network_survey(NetworkSurveyResult& result, uint32_t start_ch, uint32_t end_ch) {
    AtResponse response;
    char cmd[64];
    if (start_ch == 0 && end_ch == 0) {
        snprintf(cmd, sizeof(cmd), "AT#CSURVC");
    } else {
        snprintf(cmd, sizeof(cmd), "AT#CSURVC=%u,%u", static_cast<unsigned>(start_ch), static_cast<unsigned>(end_ch));
    }
    auto status = send_raw(cmd, response, 120000);
    if (status != ModemStatus::ok) {
        return status;
    }

    result = {};

    // Split body on AT_TERMINATOR and parse each line
    std::string_view body = response.body.view();
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end = body.find(terminator, start);
        std::string_view line = (end == std::string_view::npos) ? body.substr(start) : body.substr(start, end - start);
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        if (line.empty() || line.substr(0, 22) == "Network survey started") {
            continue;
        }

        // "Network survey ended" — with or without "(Carrier: N BCCh: M)" suffix
        if (line.rfind("Network survey ended", 0) == 0) {
            result.fHasSummary = true;
            auto carrier_pos = line.find("Carrier:");
            auto bcch_pos = line.find("BCCh:");
            if (carrier_pos != std::string_view::npos && bcch_pos != std::string_view::npos) {
                // Extract numbers after labels
                char num_buf[16];
                auto cp = carrier_pos + 8; // strlen("Carrier:")
                while (cp < line.size() && line[cp] == ' ') ++cp;
                auto cp_end = line.find(' ', cp);
                if (cp_end == std::string_view::npos) cp_end = line.size();
                size_t len = std::min(cp_end - cp, sizeof(num_buf) - 1);
                std::memcpy(num_buf, line.data() + cp, len);
                num_buf[len] = '\0';
                result.no_arfcn = std::atoi(num_buf);

                auto bp = bcch_pos + 5; // strlen("BCCh:")
                while (bp < line.size() && line[bp] == ' ') ++bp;
                auto bp_end = line.find(' ', bp);
                if (bp_end == std::string_view::npos) bp_end = line.size();
                len = std::min(bp_end - bp, sizeof(num_buf) - 1);
                std::memcpy(num_buf, line.data() + bp, len);
                num_buf[len] = '\0';
                result.no_bcch = std::atoi(num_buf);
            }
            continue;
        }

        result.cells.push_back(parse_surv_line(line));
    }

    return ModemStatus::ok;
}

// needs reboot to take effect
ModemStatus xE310::set_iot_tech(RadioTech tech, uint8_t gsm_priority) {
    AtResponse response;
    uint8_t n = 0;
#ifdef ME310M1
    if (tech == RadioTech::cat_m1)
        n = 0;
    else if (tech == RadioTech::nb_iot)
        n = 1;
    else
        return ModemStatus::invalid_param;
    gsm_priority = 0;
#endif

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#WS46=%u,%u", static_cast<unsigned>(n), static_cast<unsigned>(gsm_priority));
    return send_raw(cmd, response);
}

ModemStatus xE310::get_iot_tech(RadioTech& tech, uint8_t& gsm_priority) {
    AtResponse response;
    auto status = send_raw("AT#WS46?", response, 5000);
    if (status == ModemStatus::ok) {
        // Response body: "#WS46: <n>,<GSM_P>"
        std::string_view body = response.body.view();
        auto colon = body.find(':');
        if (colon != std::string_view::npos) {
            std::string_view params = body.substr(colon + 1);
            auto comma = params.find(',');
            unsigned int nv = 0, gp = 0;
            if (comma != std::string_view::npos) {
                char buf[16];
                size_t len = std::min(comma, sizeof(buf) - 1);
                std::memcpy(buf, params.data(), len);
                buf[len] = '\0';
                nv = static_cast<unsigned int>(std::strtoul(buf, nullptr, 10));

                auto rest = params.substr(comma + 1);
                len = std::min(rest.size(), sizeof(buf) - 1);
                std::memcpy(buf, rest.data(), len);
                buf[len] = '\0';
                gp = static_cast<unsigned int>(std::strtoul(buf, nullptr, 10));
            }
            if (nv == 0)
                tech = RadioTech::cat_m1;
            else if (nv == 1)
                tech = RadioTech::nb_iot;
            else
                tech = RadioTech::unknown;
            gsm_priority = static_cast<uint8_t>(gp);
        }
    }
    return status;
}

ModemStatus xE310::set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask, uint64_t tdscdma_mask,
                             uint64_t lte_mask_over_64) {
    AtResponse response;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT#BND=%llu,%llu,%llu,%llu,%llu", (unsigned long long)gsm_mask,
             (unsigned long long)umts_mask, (unsigned long long)lte_mask, (unsigned long long)tdscdma_mask,
             (unsigned long long)lte_mask_over_64);
    return send_raw(cmd, response);
}

ModemStatus xE310::get_bands(BandConfig& bands) {
    AtResponse response;
    auto status = send_raw("AT#BND?", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    std::string_view body = response.body.view();
    auto colon = body.find(':');
    if (colon == std::string_view::npos) {
        return ModemStatus::at_error;
    }

    std::string_view params = body.substr(colon + 1);

    // Parse up to 5 comma-separated uint64 fields
    char field_buf[5][32];
    int field_count = 0;
    std::string_view sv = params;
    while (!sv.empty() && field_count < 5) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        // Trim
        auto fs = token.find_first_not_of(" \t\"");
        auto fe = token.find_last_not_of(" \t\"");
        if (fs == std::string_view::npos) {
            field_buf[field_count][0] = '\0';
        } else {
            auto trimmed = token.substr(fs, fe - fs + 1);
            size_t len = std::min(trimmed.size(), sizeof(field_buf[0]) - 1);
            std::memcpy(field_buf[field_count], trimmed.data(), len);
            field_buf[field_count][len] = '\0';
        }
        ++field_count;
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (field_count < 5) {
        return ModemStatus::at_error;
    }

    auto parse_u64 = [](const char* s, uint64_t& out) -> bool {
        char* parse_end = nullptr;
        unsigned long long value = std::strtoull(s, &parse_end, 10);
        if (parse_end == s || *parse_end != '\0') {
            return false;
        }
        out = static_cast<uint64_t>(value);
        return true;
    };

    BandConfig parsed;
    if (!parse_u64(field_buf[0], parsed.gsm_mask) || !parse_u64(field_buf[1], parsed.umts_mask) ||
        !parse_u64(field_buf[2], parsed.lte_mask) || !parse_u64(field_buf[3], parsed.tdscdma_mask) ||
        !parse_u64(field_buf[4], parsed.lte_mask_over_64)) {
        return ModemStatus::at_error;
    }

    bands = parsed;
    return ModemStatus::ok;
}

ModemStatus xE310::set_registration_urc(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT+CEREG=4" : "AT+CEREG=0", response);
}

ModemStatus xE310::delete_mru_list(MruListRat rat) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT%%TRSHCMD=\"BSPFILE\",\"ERASE_LTEPP\",%u", static_cast<unsigned>(rat));
    return send_raw(cmd, response);
}

ModemStatus xE310::get_registration_status(RegistrationInfo& info, RadioTech tech) {
    AtResponse response;
    const char* cmd = (tech == RadioTech::gsm) ? "AT+CREG?" : "AT+CEREG?";
    auto result = send_raw(cmd, response, 5000);
    if (result != ModemStatus::ok) {
        return result;
    }

    // Response body: "+CEREG: <mode>,<stat>[,<lac>,<ci>[,<AcT>]]"
    std::string_view body = response.body.view();
    auto colon_pos = body.find(':');
    if (colon_pos == std::string_view::npos) {
        return ModemStatus::at_error;
    }

    std::string_view params = body.substr(colon_pos + 1);

    // Parse comma-separated fields into stack array
    char field_buf[8][32];
    int field_count = 0;
    size_t pos = 0;
    while (pos < params.size() && field_count < 8) {
        auto comma = params.find(',', pos);
        std::string_view token =
            (comma == std::string_view::npos) ? params.substr(pos) : params.substr(pos, comma - pos);
        // Trim leading/trailing whitespace and quotes
        auto fs = token.find_first_not_of(" \t\"");
        auto fe = token.find_last_not_of(" \t\"");
        if (fs == std::string_view::npos) {
            field_buf[field_count][0] = '\0';
        } else {
            auto trimmed = token.substr(fs, fe - fs + 1);
            size_t len = std::min(trimmed.size(), sizeof(field_buf[0]) - 1);
            std::memcpy(field_buf[field_count], trimmed.data(), len);
            field_buf[field_count][len] = '\0';
        }
        ++field_count;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }

    if (field_count >= 2) {
        info.mode = static_cast<uint8_t>(std::atoi(field_buf[0]));
        info.stat = static_cast<RegStatus>(std::atoi(field_buf[1]));
    }

    if (field_count >= 4) {
        info.lac = field_buf[2];
        info.ci = field_buf[3];
        info.fHasLocation = true;
    }

    if (field_count >= 5) {
        info.act = static_cast<RadioTech>(std::atoi(field_buf[4]));
    }

    return ModemStatus::ok;
}

ModemStatus xE310::network_attach() {
    AtResponse response;
    return send_raw("AT+CGATT=1", response, 30000);
}

ModemStatus xE310::network_detach() {
    AtResponse response;
    return send_raw("AT+CGATT=0", response, 30000);
}

ModemStatus xE310::get_signal_quality(SignalQuality& sq) {
    AtResponse response;
    auto status = send_raw("AT+CESQ", response);
    if (status == ModemStatus::ok) {
        // Response: +CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>
        std::string_view body = response.body.view();
        auto pos = body.find(':');
        if (pos != std::string_view::npos) {
            std::string_view values = body.substr(pos + 1);
            int parsed[6] = {};
            int parsed_count = 0;
            std::string_view sv = values;
            while (!sv.empty() && parsed_count < 6) {
                auto comma = sv.find(',');
                auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
                auto first = token.find_first_not_of(" \t");
                auto last_c = token.find_last_not_of(" \t");
                if (first != std::string_view::npos) {
                    char buf[16];
                    auto trimmed = token.substr(first, last_c - first + 1);
                    size_t len = std::min(trimmed.size(), sizeof(buf) - 1);
                    std::memcpy(buf, trimmed.data(), len);
                    buf[len] = '\0';
                    parsed[parsed_count] = std::atoi(buf);
                }
                ++parsed_count;
                if (comma == std::string_view::npos) break;
                sv = sv.substr(comma + 1);
            }

            if (parsed_count >= 6) {
                sq.rssi = parsed[0];
                sq.ber = parsed[1];
                sq.rsrq = parsed[4];
                sq.rsrp = parsed[5];
            }
        }
    }
    return status;
}

ModemStatus xE310::set_radio_tech(RadioTech tech) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+COPS=0,,,%d", static_cast<int>(tech));
    return send_raw(cmd, response, 45000);
}

ModemStatus xE310::set_operator_manual(std::string_view oper, RadioTech tech) {
    AtResponse response;
    if (oper.empty()) {
        return set_operator_auto();
    } else {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+COPS=1,2,\"%.*s\",%d", (int)oper.size(), oper.data(), static_cast<int>(tech));
        return send_raw(cmd, response, 210000);
    }
}

ModemStatus xE310::set_operator_auto() {
    AtResponse response;
    return send_raw("AT+COPS=0", response, 210000);
}

ModemStatus xE310::get_operator(FixedString<MODEM_MEDIUM_STR>& oper) {
    AtResponse response;
    auto status = send_raw("AT+COPS?", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Response body: "+COPS: <mode>[,<format>,<oper>,<act>]"
    std::string_view body = response.body.view();
    auto colon = body.find(':');
    if (colon == std::string_view::npos) {
        return ModemStatus::at_error;
    }

    std::string_view params = body.substr(colon + 1);

    // Find the third comma-separated field (oper)
    int field_idx = 0;
    size_t pos = 0;
    while (pos < params.size() && field_idx < 3) {
        auto comma = params.find(',', pos);
        if (field_idx == 2) {
            // This is the oper field
            std::string_view token =
                (comma == std::string_view::npos) ? params.substr(pos) : params.substr(pos, comma - pos);
            auto a = token.find_first_not_of(" \t\"");
            auto b = token.find_last_not_of(" \t\"");
            oper = (a == std::string_view::npos) ? std::string_view("") : token.substr(a, b - a + 1);
            break;
        }
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
        ++field_idx;
    }

    return ModemStatus::ok;
}

ModemStatus xE310::get_available_operators(StaticVector<Operator, MAX_OPERATORS>& operators) {
    AtResponse response;
    auto status = send_raw("AT+COPS=?", response, 210000);
    if (status != ModemStatus::ok) {
        return status;
    }

    operators.clear();

    // Response body example:
    // +COPS: (2,"Vodafone PT","Vodafone","26801",8),(1,"NOS","NOS","26803",8),...
    std::string_view body = response.body.view();
    size_t pos = 0;
    while (pos < body.size()) {
        auto open = body.find('(', pos);
        if (open == std::string_view::npos) break;
        auto close = body.find(')', open);
        if (close == std::string_view::npos) break;

        std::string_view entry = body.substr(open + 1, close - open - 1);
        pos = close + 1;

        // Tokenize comma-separated fields, respecting quoted strings
        char field_buf[5][64];
        int field_count = 0;
        size_t fp = 0;
        while (fp <= entry.size() && field_count < 5) {
            // skip whitespace
            while (fp < entry.size() && entry[fp] == ' ') ++fp;
            if (fp >= entry.size()) break;

            size_t start_pos = fp;
            if (entry[fp] == '"') {
                ++fp;
                while (fp < entry.size() && entry[fp] != '"') ++fp;
                if (fp < entry.size()) ++fp; // skip closing quote
                while (fp < entry.size() && entry[fp] != ',') ++fp;
            } else {
                while (fp < entry.size() && entry[fp] != ',') ++fp;
            }
            std::string_view fld = entry.substr(start_pos, fp - start_pos);
            // strip quotes
            auto a = fld.find_first_not_of(" \t\"");
            auto b = fld.find_last_not_of(" \t\"");
            std::string_view stripped = (a == std::string_view::npos) ? std::string_view("") : fld.substr(a, b - a + 1);
            size_t len = std::min(stripped.size(), sizeof(field_buf[0]) - 1);
            std::memcpy(field_buf[field_count], stripped.data(), len);
            field_buf[field_count][len] = '\0';
            ++field_count;
            if (fp < entry.size() && entry[fp] == ',') ++fp;
        }

        if (field_count < 4) continue;

        Operator op;
        // fields[0] = stat (skip), [1] = long_name, [2] = short_name, [3] = numeric, [4] = act
        op.long_name = std::string_view(field_buf[1]);
        op.short_name = std::string_view(field_buf[2]);
        op.numeric = std::string_view(field_buf[3]);
        op.act = (field_count >= 5) ? static_cast<RadioTech>(std::atoi(field_buf[4])) : RadioTech::gsm;
        operators.push_back(op);
    }

    return ModemStatus::ok;
}

ModemStatus xE310::scan_networks(CsurvResult& result, uint32_t start_ch, uint32_t end_ch) {
    AtResponse response;

    // Set numeric output format and enable summary line.
    auto status = send_raw("AT#CSURVF=2", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    char cmd[64];
    if (start_ch == 0 && end_ch == 0) {
        snprintf(cmd, sizeof(cmd), "AT#CSURV");
    } else {
        snprintf(cmd, sizeof(cmd), "AT#CSURV=%u,%u", static_cast<unsigned>(start_ch), static_cast<unsigned>(end_ch));
    }
    status = send_raw(cmd, response, 120000);
    if (status != ModemStatus::ok) {
        return status;
    }

    result = {};

    // Extract the value following a labeled key
    auto extract_value = [](std::string_view line, const char* label) -> std::string_view {
        auto pos = line.find(label);
        if (pos == std::string_view::npos) return {};
        pos += std::strlen(label);
        while (pos < line.size() && line[pos] == ' ') ++pos;
        auto end = line.find(' ', pos);
        return (end == std::string_view::npos) ? line.substr(pos) : line.substr(pos, end - pos);
    };

    std::string_view body = response.body.view();
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end = body.find(terminator, start);
        std::string_view line = (end == std::string_view::npos) ? body.substr(start) : body.substr(start, end - start);
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        if (line.empty() || line.substr(0, 22) == "Network survey started") {
            continue;
        }

        // Summary line: "Network survey ended (Carrier: <N> BCCh: <M>)"
        if (line.rfind("Network survey ended", 0) == 0) {
            result.fHasSummary = true;
            auto carrier_pos = line.find("Carrier:");
            auto bcch_pos = line.find("BCCh:");
            if (carrier_pos != std::string_view::npos && bcch_pos != std::string_view::npos) {
                char num_buf[16];
                auto cp = carrier_pos + 8;
                while (cp < line.size() && line[cp] == ' ') ++cp;
                auto cp_end = line.find(' ', cp);
                if (cp_end == std::string_view::npos) cp_end = line.size();
                size_t len = std::min(cp_end - cp, sizeof(num_buf) - 1);
                std::memcpy(num_buf, line.data() + cp, len);
                num_buf[len] = '\0';
                result.no_arfcn = std::atoi(num_buf);

                auto bp = bcch_pos + 5;
                while (bp < line.size() && line[bp] == ' ') ++bp;
                auto bp_end = line.find(' ', bp);
                if (bp_end == std::string_view::npos) bp_end = line.size();
                len = std::min(bp_end - bp, sizeof(num_buf) - 1);
                std::memcpy(num_buf, line.data() + bp, len);
                num_buf[len] = '\0';
                result.no_bcch = std::atoi(num_buf);
            }
            continue;
        }

        auto earfcn_str = extract_value(line, "earfcn:");
        if (earfcn_str.empty()) continue;

        CsurvCell cell;
        char vbuf[32];
        auto to_cstr = [&vbuf](std::string_view sv) -> const char* {
            size_t len = std::min(sv.size(), sizeof(vbuf) - 1);
            std::memcpy(vbuf, sv.data(), len);
            vbuf[len] = '\0';
            return vbuf;
        };

        cell.earfcn = std::atoi(to_cstr(earfcn_str));
        cell.rx_lev = std::atoi(to_cstr(extract_value(line, "rxLev:")));
        cell.mcc = static_cast<uint16_t>(std::strtoul(to_cstr(extract_value(line, "mcc:")), nullptr, 16));
        cell.mnc = static_cast<uint16_t>(std::strtoul(to_cstr(extract_value(line, "mnc:")), nullptr, 16));
        cell.cell_id = static_cast<uint32_t>(std::strtoul(to_cstr(extract_value(line, "cellid:")), nullptr, 16));
        cell.tac = static_cast<uint32_t>(std::strtoul(to_cstr(extract_value(line, "tac:")), nullptr, 16));
        cell.cell_identity =
            static_cast<uint64_t>(std::strtoull(to_cstr(extract_value(line, "cellIdentity:")), nullptr, 16));

        result.cells.push_back(cell);
    }

    return ModemStatus::ok;
}

// --- Network Attach ---

ModemStatus xE310::set_apn(uint8_t cid, std::string_view apn) {
    AtResponse response;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=%u,\"IP\",\"%.*s\"", static_cast<unsigned>(cid), (int)apn.size(),
             apn.data());
    return send_raw(cmd, response);
}

ModemStatus xE310::get_apn(uint8_t cid, FixedString<MODEM_MEDIUM_STR>& apn) {
    AtResponse response;
    auto status = send_raw("AT+CGDCONT?", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    std::string_view body = response.body.view();
    std::string_view terminator = AT_TERMINATOR;
    std::string_view::size_type start = 0;

    while (start < body.size()) {
        auto end = body.find(terminator, start);
        std::string_view line = (end == std::string_view::npos) ? body.substr(start) : body.substr(start, end - start);
        start = (end == std::string_view::npos) ? body.size() : end + terminator.size();

        // Find the prefix "+CGDCONT:"
        constexpr std::string_view PREFIX = "+CGDCONT:";
        auto p = line.find(PREFIX);
        if (p == std::string_view::npos) continue;

        std::string_view params = line.substr(p + PREFIX.size());

        // Find the CID (first field) and APN (third field)
        int field_idx = 0;
        size_t pos = 0;
        bool fFoundCid = false;
        while (pos < params.size()) {
            auto comma = params.find(',', pos);
            std::string_view token =
                (comma == std::string_view::npos) ? params.substr(pos) : params.substr(pos, comma - pos);
            // Strip whitespace and quotes
            auto a = token.find_first_not_of(" \t\"");
            auto b = token.find_last_not_of(" \t\"");
            std::string_view stripped =
                (a == std::string_view::npos) ? std::string_view("") : token.substr(a, b - a + 1);

            if (field_idx == 0) {
                char buf[8];
                size_t len = std::min(stripped.size(), sizeof(buf) - 1);
                std::memcpy(buf, stripped.data(), len);
                buf[len] = '\0';
                if (static_cast<uint8_t>(std::atoi(buf)) == cid) {
                    fFoundCid = true;
                } else {
                    break;
                }
            } else if (field_idx == 2 && fFoundCid) {
                apn = stripped;
                return ModemStatus::ok;
            }

            ++field_idx;
            if (comma == std::string_view::npos) break;
            pos = comma + 1;
        }
    }

    return ModemStatus::at_error;
}

ModemStatus xE310::set_pdp_urc(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT+CGEREP=1" : "AT+CGEREP=0", response);
}

ModemStatus xE310::disable_all_notifyev() {
    AtResponse response;
    return send_raw("AT%NOTIFYEV=\"ALL\",0", response);
}

ModemStatus xE310::set_plmnsearchexh_notify(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT%NOTIFYEV=\"PLMNSEARCHEXH\",1" : "AT%NOTIFYEV=\"PLMNSEARCHEXH\",0", response);
}

ModemStatus xE310::activate_pdp(uint8_t cid) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SGACT=1,%u", static_cast<unsigned>(cid));
    return send_raw(cmd, response);
}

ModemStatus xE310::deactivate_pdp(uint8_t cid) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SGACT=0,%u", static_cast<unsigned>(cid));
    return send_raw(cmd, response);
}

ModemStatus xE310::get_pdp_state(uint8_t cid, bool& active) {
    AtResponse response;
    auto status = send_raw("AT#SGACT?", response);
    if (status == ModemStatus::ok) {
        char search[8];
        snprintf(search, sizeof(search), "%u,", static_cast<unsigned>(cid));
        auto pos = response.body.find(search);
        if (pos != FixedString<AT_RESPONSE_MAX>::NPOS) {
            active = (response.body[pos + 2] == '1');
        } else {
            active = false;
        }
    }
    return status;
}

ModemStatus xE310::get_ip_address(uint8_t cid, FixedString<MODEM_IP_STR>& ip_addr) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGPADDR=%u", static_cast<unsigned>(cid));
    auto status = send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        std::string_view body = response.body.view();
        auto start_q = body.find('"');
        auto end_q = body.rfind('"');
        if (start_q != std::string_view::npos && end_q != std::string_view::npos && end_q > start_q) {
            ip_addr = body.substr(start_q + 1, end_q - start_q - 1);
        }
    }
    return status;
}

ModemStatus xE310::get_pdp_info(uint8_t cid, FixedString<MODEM_IP_STR>& ip_addr, FixedString<MODEM_IP_STR>& gw_addr,
                                FixedString<MODEM_IP_STR>& dns_primary, FixedString<MODEM_IP_STR>& dns_secondary) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGCONTRDP=%u", static_cast<unsigned>(cid));
    auto status = send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        std::string_view body = response.body.view();
        size_t pos = 0;
        int field = 0;
        while (pos < body.size()) {
            auto start_q = body.find('"', pos);
            if (start_q == std::string_view::npos) break;
            auto end_q = body.find('"', start_q + 1);
            if (end_q == std::string_view::npos) break;

            auto value = body.substr(start_q + 1, end_q - start_q - 1);
            switch (field) {
                case 1: ip_addr = value; break;
                case 2: gw_addr = value; break;
                case 3: dns_primary = value; break;
                case 4: dns_secondary = value; break;
                default: break;
            }
            ++field;
            pos = end_q + 1;
        }
    }
    return status;
}

// --- UDP Connection ---

ModemStatus xE310::udp_open(uint8_t conn_id, std::string_view host, uint16_t remote_port, uint16_t local_port) {
    if (local_port == 0) local_port = remote_port;
    AtResponse response;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT#SD=%u,1,%u,\"%.*s\",0,%u,1,0,1", static_cast<unsigned>(conn_id),
             static_cast<unsigned>(remote_port), (int)host.size(), host.data(), static_cast<unsigned>(local_port));
    return send_raw(cmd, response);
}

ModemStatus xE310::udp_listen(uint8_t conn_id, uint16_t local_port, uint8_t cid) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT#SL=%u,1,%u,255,%u", static_cast<unsigned>(conn_id),
             static_cast<unsigned>(local_port), static_cast<unsigned>(cid));
    return send_raw(cmd, response);
}

ModemStatus xE310::udp_send(uint8_t conn_id, const uint8_t* data, size_t length) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT#SSENDEXT=%u,%zu", static_cast<unsigned>(conn_id), length);
    return controller_.send_with_prompt(cmd, data, length, response);
}

ModemStatus xE310::udp_receive(uint8_t conn_id, StaticVector<uint8_t, UDP_MAX_BYTES>& data, uint16_t max_bytes) {
    AtResponse response;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT#SRECV=%u,%u", static_cast<unsigned>(conn_id), static_cast<unsigned>(max_bytes));
    auto status = send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        std::string_view body = response.body.view();
        auto nl_pos = body.find('\n');
        if (nl_pos != std::string_view::npos && nl_pos + 1 < body.size()) {
            auto payload = body.substr(nl_pos + 1);
            data.assign(reinterpret_cast<const uint8_t*>(payload.data()),
                        reinterpret_cast<const uint8_t*>(payload.data() + payload.size()));
        } else {
            data.clear();
        }
    }
    return status;
}

ModemStatus xE310::udp_close(uint8_t conn_id) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SH=%u", static_cast<unsigned>(conn_id));
    return send_raw(cmd, response);
}

ModemStatus xE310::udp_status(uint8_t conn_id, uint8_t& state) {
    AtResponse response;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SS=%u", static_cast<unsigned>(conn_id));
    auto status = send_raw(cmd, response);
    if (status == ModemStatus::ok) {
        auto comma_pos = response.body.find(',');
        if (comma_pos != FixedString<AT_RESPONSE_MAX>::NPOS) {
            state = static_cast<uint8_t>(std::atoi(response.body.c_str() + comma_pos + 1));
        }
    }
    return status;
}

ModemStatus xE310::send_at_command(std::string_view command, FixedString<AT_RESPONSE_MAX>& response,
                                   uint32_t timeout_ms) {
    AtResponse at_response;
    auto status = send_raw(command, at_response, timeout_ms);
    if (status == ModemStatus::ok) {
        response = at_response.body.view();
    }
    return status;
}

// --- Power Options ---

// --- GNSS ---

ModemStatus xE310::set_gnss_power(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT$GPSP=1" : "AT$GPSP=0", response);
}

ModemStatus xE310::set_gnss_urc(bool enable) {
    AtResponse response;
    return send_raw(enable ? "AT$GNSSNMEA=1" : "AT$GNSSNMEA=0", response);
}

ModemStatus xE310::get_gnss_position(GnssPosition& pos) {
    AtResponse response;
    auto status = send_raw("AT$GPSACP", response);
    if (status != ModemStatus::ok) {
        return status;
    }

    // Response body: "$GPSACP: <UTC>,<latitude>,<longitude>,<hdop>,<altitude>,<fix>,<cog>,<spkm>,<spkn>,<date>,
    //                 <nsat>,<hepe>,<vepe>"
    std::string_view body = response.body.view();
    auto colon = body.find(':');
    if (colon == std::string_view::npos) {
        return ModemStatus::at_error;
    }
    std::string_view params = body.substr(colon + 1);

    // Tokenize on commas
    FixedString<MODEM_SHORT_STR> fields[13];
    int field_count = 0;
    std::string_view sv = params;
    while (!sv.empty() && field_count < 13) {
        auto comma = sv.find(',');
        auto token = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
        auto start = token.find_first_not_of(' ');
        fields[field_count] = (start == std::string_view::npos) ? std::string_view("") : token.substr(start);
        ++field_count;
        if (comma == std::string_view::npos) break;
        sv = sv.substr(comma + 1);
    }

    if (field_count >= 1) pos.utc = fields[0];
    if (field_count >= 2) pos.latitude = fields[1];
    if (field_count >= 3) pos.longitude = fields[2];
    if (field_count >= 4) pos.hdop = fields[3];
    if (field_count >= 5) pos.altitude = fields[4];
    if (field_count >= 6) pos.fix = static_cast<GnssFixType>(std::atoi(fields[5].c_str()));
    if (field_count >= 7) pos.cog = fields[6];
    if (field_count >= 8) pos.spkm = fields[7];
    if (field_count >= 9) pos.spkn = fields[8];
    if (field_count >= 10) pos.date = fields[9];
    if (field_count >= 11) pos.nsat = static_cast<uint8_t>(std::atoi(fields[10].c_str()));
    if (field_count >= 12) pos.hepe = fields[11];
    if (field_count >= 13) pos.vepe = fields[12];

    return ModemStatus::ok;
}

// --- Event Handlers ---
const RegistrationInfo& xE310::registration_info() const {
    return info;
}

StaticVector<FixedString<URC_LINE_MAX>, ModemController::MAX_URC_LINES> xE310::poll_urc(uint32_t timeout_ms) {
    return controller_.poll_urc(timeout_ms);
}

} // namespace modem
