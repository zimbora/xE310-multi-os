#pragma once
// RPC helpers: JSON serialization for NetworkLte state types and NetworkLteConfig
// SET CONFIG key=value parser. Used by main.cpp for port 9003.

#include "modem/network_lte.h"
#include <string>
#include <cstdio>
#include <stdexcept>

namespace rpc {

// ── Enum → string ─────────────────────────────────────────────────────────────

inline const char* to_str(modem::NetworkLteState v) {
    switch (v) {
        case modem::NetworkLteState::none:                return "none";
        case modem::NetworkLteState::rebooting:           return "rebooting";
        case modem::NetworkLteState::switched_off:        return "switched_off";
        case modem::NetworkLteState::off_mode:            return "off_mode";
        case modem::NetworkLteState::sleep_mode:          return "sleep_mode";
        case modem::NetworkLteState::setup_mode:          return "setup_mode";
        case modem::NetworkLteState::idle_mode:           return "idle_mode";
        case modem::NetworkLteState::network_detached:    return "network_detached";
        case modem::NetworkLteState::network_attaching:   return "network_attaching";
        case modem::NetworkLteState::pdp_context_closed:  return "pdp_context_closed";
        case modem::NetworkLteState::pdp_context_opening: return "pdp_context_opening";
        case modem::NetworkLteState::data_ready:          return "data_ready";
        case modem::NetworkLteState::transparent_mode:    return "transparent_mode";
        case modem::NetworkLteState::modem_fota:          return "modem_fota";
        case modem::NetworkLteState::done:                return "done";
    }
    return "unknown";
}

inline const char* to_str(modem::SimStatus v) {
    switch (v) {
        case modem::SimStatus::not_inserted:              return "not_inserted";
        case modem::SimStatus::inserted:                  return "inserted";
        case modem::SimStatus::inserted_and_pin_unlocked: return "inserted_and_pin_unlocked";
        case modem::SimStatus::inserted_and_ready:        return "inserted_and_ready";
    }
    return "unknown";
}

inline const char* to_str(modem::RadioTech v) {
    switch (v) {
        case modem::RadioTech::gsm:    return "gsm";
        case modem::RadioTech::lte:    return "lte";
        case modem::RadioTech::cat_m1: return "cat_m1";
        case modem::RadioTech::nb_iot: return "nb_iot";
    }
    return "unknown";
}

inline const char* to_str(modem::RegStatus v) {
    switch (v) {
        case modem::RegStatus::not_registered:    return "not_registered";
        case modem::RegStatus::registered_home:   return "registered_home";
        case modem::RegStatus::searching:         return "searching";
        case modem::RegStatus::denied:            return "denied";
        case modem::RegStatus::unknown:           return "unknown";
        case modem::RegStatus::registered_roaming:return "registered_roaming";
        case modem::RegStatus::sms_only:          return "sms_only";
        case modem::RegStatus::sms_only_roaming:  return "sms_only_roaming";
        case modem::RegStatus::emergency:         return "emergency";
        case modem::RegStatus::csfb_home:         return "csfb_home";
        case modem::RegStatus::csfb_roaming:      return "csfb_roaming";
    }
    return "unknown";
}

inline const char* to_str(modem::PsmMode v) {
    return v == modem::PsmMode::enable ? "enable" : "disable";
}

inline const char* to_str(modem::ContextState v) {
    return v == modem::ContextState::active ? "active" : "inactive";
}

inline const char* to_str(modem::ServerState v) {
    switch (v) {
        case modem::ServerState::unknown:      return "unknown";
        case modem::ServerState::disconnected: return "disconnected";
        case modem::ServerState::connected:    return "connected";
    }
    return "unknown";
}

inline const char* to_str(modem::SurvCellType v) {
    switch (v) {
        case modem::SurvCellType::cell_2g_bcch:     return "2g_bcch";
        case modem::SurvCellType::cell_2g_non_bcch: return "2g_non_bcch";
        case modem::SurvCellType::cell_4g:          return "4g";
    }
    return "unknown";
}

// ── JSON string escaping ──────────────────────────────────────────────────────

inline std::string json_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    out += '"';
    return out;
}

// ── Struct serializers ────────────────────────────────────────────────────────

inline std::string to_json(const modem::SoftwarePackageVersion& v) {
    return "{\"package_version\":"    + json_str(v.package_version)     + ","
           "\"modem_version\":"       + json_str(v.modem_version)       + ","
           "\"prod_params_version\":" + json_str(v.prod_params_version) + ","
           "\"app_version\":"         + json_str(v.app_version)         + "}";
}

inline std::string to_json(const modem::ModemInfo& v) {
    return "{\"imei\":"            + json_str(v.imei)           + ","
           "\"imei_sv\":"          + json_str(v.imei_sv)        + ","
           "\"iccid\":"            + json_str(v.iccid)          + ","
           "\"imsi\":"             + json_str(v.imsi)           + ","
           "\"model_id\":"         + json_str(v.model_id)       + ","
           "\"telit_id\":"         + json_str(v.telit_id)       + ","
           "\"identification\":"   + json_str(v.identification) + ","
           "\"sw_package_version\":" + to_json(v.sw_package_version) + "}";
}

inline std::string to_json(const modem::RegistrationInfo& v) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"mode\":%u,\"stat\":\"%s\",\"lac\":%s,\"ci\":%s,"
        "\"act\":\"%s\",\"has_location\":%s,"
        "\"cause_type\":%u,\"reject_cause\":%u,\"has_reject\":%s,"
        "\"active_time\":%s,\"periodic_tau\":%s,\"has_psm\":%s}",
        (unsigned)v.mode, to_str(v.stat),
        json_str(v.lac).c_str(), json_str(v.ci).c_str(),
        to_str(v.act), v.has_location ? "true" : "false",
        (unsigned)v.cause_type, (unsigned)v.reject_cause,
        v.has_reject ? "true" : "false",
        json_str(v.active_time).c_str(), json_str(v.periodic_tau).c_str(),
        v.has_psm ? "true" : "false");
    return buf;
}

inline std::string to_json(const modem::NetworkInfo& v) {
    return "{\"context_state\":\"" + std::string(to_str(v.context_state)) + "\","
           "\"ip_address\":"       + json_str(v.ip_address) + "}";
}

inline std::string to_json(const modem::SignalQuality& v) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"rssi\":%d,\"ber\":%d,\"rsrq\":%d,\"rsrp\":%d,\"rsrp_dbm\":%d}",
        v.rssi, v.ber, v.rsrq, v.rsrp, v.rsrp_dbm());
    return buf;
}

inline std::string to_json(const modem::CpsmsConfig& v) {
    return "{\"mode\":\""             + std::string(to_str(v.mode))  + "\","
           "\"req_periodic_rau\":"     + json_str(v.req_periodic_rau)     + ","
           "\"req_gprs_ready_timer\":" + json_str(v.req_gprs_ready_timer) + ","
           "\"req_periodic_tau\":"     + json_str(v.req_periodic_tau)     + ","
           "\"req_active_time\":"      + json_str(v.req_active_time)      + "}";
}

inline std::string to_json(const modem::TelitCpsmsConfig& v) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\","
        "\"has_periodic_rau\":%s,\"req_periodic_rau\":%u,"
        "\"has_gprs_ready_timer\":%s,\"req_gprs_ready_timer\":%u,"
        "\"has_periodic_tau\":%s,\"req_periodic_tau\":%u,"
        "\"has_active_time\":%s,\"req_active_time\":%u,"
        "\"has_psm_version\":%s,\"psm_version\":%u,"
        "\"has_psm_threshold\":%s,\"psm_threshold\":%u}",
        to_str(v.mode),
        v.has_periodic_rau    ? "true" : "false", v.req_periodic_rau,
        v.has_gprs_ready_timer? "true" : "false", v.req_gprs_ready_timer,
        v.has_periodic_tau    ? "true" : "false", v.req_periodic_tau,
        v.has_active_time     ? "true" : "false", v.req_active_time,
        v.has_psm_version     ? "true" : "false", (unsigned)v.psm_version,
        v.has_psm_threshold   ? "true" : "false", v.psm_threshold);
    return buf;
}

inline std::string to_json(const modem::TelitCpsmsStatus& v) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"status\":%u,\"t3324\":%u,\"t3412\":%u,"
        "\"psm_version\":%u,\"psm_threshold\":%u,\"mode\":\"%s\"}",
        (unsigned)v.status, v.t3324, v.t3412,
        (unsigned)v.psm_version, v.psm_threshold, to_str(v.mode));
    return buf;
}

inline std::string to_json(const modem::SurvCell& c) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"%s\",\"arfcn\":%d,\"bsic\":%d,\"rx_lev\":%d,\"ber\":%d,"
        "\"mcc\":%u,\"mnc\":%u,\"lac\":%u,\"cell_id\":%u,\"cell_stat\":%s,"
        "\"num_arfcn\":%d,\"earfcn\":%d,\"tac\":%u,\"phys_cell_id\":%u,"
        "\"cell_identity\":%llu,\"rsrp\":%.1f,\"rsrq\":%.1f}",
        to_str(c.type), c.arfcn, c.bsic, c.rx_lev, c.ber,
        (unsigned)c.mcc, (unsigned)c.mnc, c.lac, c.cell_id,
        json_str(c.cell_stat).c_str(),
        c.num_arfcn, c.earfcn, c.tac, c.phys_cell_id,
        (unsigned long long)c.cell_identity, c.rsrp, c.rsrq);
    return buf;
}

inline std::string to_json(const modem::NetworkSurveyResult& v) {
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "{\"has_summary\":%s,\"no_arfcn\":%d,\"no_bcch\":%d,\"cells\":[",
             v.has_summary ? "true" : "false", v.no_arfcn, v.no_bcch);
    std::string s = hdr;
    for (size_t i = 0; i < v.cells.size(); ++i) {
        if (i > 0) s += ',';
        s += to_json(v.cells[i]);
    }
    s += "]}";
    return s;
}

inline std::string to_json(const modem::ServerInfo& v) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"conn_id\":%u,\"state\":\"%s\",\"protocol\":%s,"
        "\"address\":%s,\"port\":%u,\"has_data\":%s}",
        (unsigned)v.conn_id, to_str(v.state),
        json_str(v.protocol).c_str(), json_str(v.address).c_str(),
        (unsigned)v.port, v.fHasData ? "true" : "false");
    return buf;
}

inline std::string config_to_json(const modem::NetworkLteConfig& c) {
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"cid\":%u,\"attach_timeout_sec\":%u,\"pdp_timeout_sec\":%u,"
        "\"data_ready_timeout_sec\":%u,\"transparent_timeout_sec\":%u,"
        "\"max_network_attempts\":%u,\"max_attach_retries\":%u,\"max_pdp_retries\":%u,"
        "\"default_lte_bands\":%llu,\"default_iot_tech\":\"%s\",\"default_apn\":%s,"
        "\"fallback_lte_bands\":%llu,\"fallback_iot_tech\":\"%s\",\"fallback_apn\":%s,"
        "\"plmn\":%s,\"psm_t3412\":%u,\"psm_t3324\":%u,\"conn_id\":%u}",
        (unsigned)c.cid,
        (unsigned)c.attach_timeout_sec, (unsigned)c.pdp_timeout_sec,
        (unsigned)c.data_ready_timeout_sec, (unsigned)c.transparent_timeout_sec,
        (unsigned)c.max_network_attempts, (unsigned)c.max_attach_retries,
        (unsigned)c.max_pdp_retries,
        (unsigned long long)c.default_lte_bands, to_str(c.default_iot_tech),
        json_str(c.default_apn).c_str(),
        (unsigned long long)c.fallback_lte_bands, to_str(c.fallback_iot_tech),
        json_str(c.fallback_apn).c_str(),
        json_str(c.plmn).c_str(), c.psm_t3412, c.psm_t3324, (unsigned)c.conn_id);
    return buf;
}

// ── SET CONFIG key=value parser ───────────────────────────────────────────────

inline modem::RadioTech radio_tech_from_str(const std::string& s) {
    if (s == "gsm")    return modem::RadioTech::gsm;
    if (s == "lte")    return modem::RadioTech::lte;
    if (s == "cat_m1") return modem::RadioTech::cat_m1;
    if (s == "nb_iot") return modem::RadioTech::nb_iot;
    return modem::RadioTech::cat_m1;
}

// Parse "key=value [key=value ...]" and apply matched fields to cfg.
// Returns true if at least one field was successfully set.
inline bool apply_config_fields(const std::string& args, modem::NetworkLteConfig& cfg) {
    bool changed = false;
    size_t pos = 0;
    while (pos < args.size()) {
        while (pos < args.size() && args[pos] == ' ') ++pos;
        if (pos >= args.size()) break;

        auto eq = args.find('=', pos);
        if (eq == std::string::npos) break;

        auto sp = args.find(' ', eq + 1);
        std::string key = args.substr(pos, eq - pos);
        std::string val = (sp == std::string::npos)
                          ? args.substr(eq + 1)
                          : args.substr(eq + 1, sp - eq - 1);
        pos = (sp == std::string::npos) ? args.size() : sp + 1;

        try {
            if      (key == "cid")                     { cfg.cid                     = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "attach_timeout_sec")       { cfg.attach_timeout_sec       = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "pdp_timeout_sec")          { cfg.pdp_timeout_sec          = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "data_ready_timeout_sec")   { cfg.data_ready_timeout_sec   = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "transparent_timeout_sec")  { cfg.transparent_timeout_sec  = (uint16_t)std::stoul(val); changed = true; }
            else if (key == "max_network_attempts")     { cfg.max_network_attempts     = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "max_attach_retries")       { cfg.max_attach_retries       = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "max_pdp_retries")          { cfg.max_pdp_retries          = (uint8_t)std::stoul(val);  changed = true; }
            else if (key == "default_lte_bands")        { cfg.default_lte_bands        = std::stoull(val);          changed = true; }
            else if (key == "default_iot_tech")         { cfg.default_iot_tech         = radio_tech_from_str(val);  changed = true; }
            else if (key == "default_apn")              { cfg.default_apn              = val;                       changed = true; }
            else if (key == "fallback_lte_bands")       { cfg.fallback_lte_bands       = std::stoull(val);          changed = true; }
            else if (key == "fallback_iot_tech")        { cfg.fallback_iot_tech        = radio_tech_from_str(val);  changed = true; }
            else if (key == "fallback_apn")             { cfg.fallback_apn             = val;                       changed = true; }
            else if (key == "plmn")                     { cfg.plmn                     = val;                       changed = true; }
            else if (key == "psm_t3412")                { cfg.psm_t3412                = std::stoul(val);           changed = true; }
            else if (key == "psm_t3324")                { cfg.psm_t3324                = std::stoul(val);           changed = true; }
            else if (key == "conn_id")                  { cfg.conn_id                  = (uint8_t)std::stoul(val);  changed = true; }
        } catch (const std::exception&) {
            // skip invalid values silently
        }
    }
    return changed;
}

} // namespace rpc
