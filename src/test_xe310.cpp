#include "modem/modem_controller.h"
#include "modem/at_command.h"
#include "modem/uart_factory.h"
#include "modem/xe310.h"
#include "modem/log.h"
#include <memory>
#include <string>

MODEM_LOG_MODULE_REGISTER(modem_app);

static void log_status(modem::ModemStatus status) {
    switch (status) {
        case modem::ModemStatus::timeout: MODEM_LOG_ERR("  -> timeout"); break;
        case modem::ModemStatus::at_error: MODEM_LOG_ERR("  -> AT command error"); break;
        case modem::ModemStatus::uart_error: MODEM_LOG_ERR("  -> UART error"); break;
        case modem::ModemStatus::not_connected: MODEM_LOG_ERR("  -> not connected"); break;
        default: MODEM_LOG_ERR("  -> error code %d", static_cast<int>(status)); break;
    }
}

int main() {
    auto uart = modem::create_platform_uart();
    modem::ModemController controller(std::move(uart));

    modem::UartConfig config;
    config.baud_rate = 115200;

    MODEM_LOG_INF("Connecting to modem on COM17...");

    auto status = controller.connect("COM17", config);
    MODEM_LOG_INF("Connection status: %d", static_cast<int>(status));
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to open modem port");
        log_status(status);
        return 1;
    }

    modem::xE310 modem(controller);

    status = modem.at_ok();
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Modem not responsive");
        log_status(status);
        controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Modem initialized successfully");
    status = modem.set_echo(false);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to disable echo");
        log_status(status);
    } else {
        MODEM_LOG_INF("Echo disabled");
    }

    modem::FixedString<modem::MODEM_SHORT_STR> imei_sv;
    status = modem.request_imei_sv(imei_sv);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IMEI/SV");
        log_status(status);
    } else {
        MODEM_LOG_INF("IMEI/SV: %s", imei_sv.c_str());
    }

    modem::FixedString<modem::MODEM_MEDIUM_STR> request_model_id;
    status = modem.request_model_id(request_model_id);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve model ID");
        log_status(status);
    } else {
        MODEM_LOG_INF("Model ID: %s", request_model_id.c_str());
    }

    modem::SoftwarePackageVersion ver;
    status = modem.request_sw_package_version(ver);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve software package version");
        log_status(status);
    } else {
        MODEM_LOG_INF("Software Package Version: %s", ver.package_version.c_str());
        MODEM_LOG_INF("Modem Version: %s", ver.modem_version.c_str());
        MODEM_LOG_INF("Production Parameters Version: %s", ver.prod_params_version.c_str());
        MODEM_LOG_INF("Application Software Version: %s", ver.app_version.c_str());
    }

    modem::FixedString<modem::MODEM_SHORT_STR> read_imei;
    status = modem.get_imei(read_imei);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IMEI");
        log_status(status);
    } else {
        MODEM_LOG_INF("IMEI: %s", read_imei.c_str());
    }

    modem::FixedString<modem::MODEM_SHORT_STR> read_iccid;
    status = modem.read_iccid(read_iccid);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve ICCID");
        log_status(status);
    } else {
        MODEM_LOG_INF("ICCID: %s", read_iccid.c_str());
    }

    modem::FixedString<modem::MODEM_SHORT_STR> read_imsi;
    status = modem.read_imsi(read_imsi);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IMSI");
        log_status(status);
    } else {
        MODEM_LOG_INF("IMSI: %s", read_imsi.c_str());
    }

    modem::TelitCpsmsStatus psm_status;
    status = modem.get_telit_psm(psm_status);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve PSM status");
        log_status(status);
    } else {
        MODEM_LOG_INF("PSM Status: %d", psm_status.status);
        MODEM_LOG_INF("T3324: %u seconds", psm_status.t3324);
        MODEM_LOG_INF("T3412: %u seconds", psm_status.t3412);
        MODEM_LOG_INF("PSM Version: %d", psm_status.psm_version);
        MODEM_LOG_INF("PSM Threshold: %u seconds", psm_status.psm_threshold);
        MODEM_LOG_INF("PSM Mode: %d", static_cast<int>(psm_status.mode));
    }

    modem::BandConfig bands;
    status = modem.get_bands(bands);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve band configuration");
        log_status(status);
    } else {
        MODEM_LOG_INF("Band Configuration - GSM: %llu, UMTS: %llu, LTE: %llu, TDSCDMA: %llu, LTE>64: %llu",
                      static_cast<unsigned long long>(bands.gsm_mask), static_cast<unsigned long long>(bands.umts_mask),
                      static_cast<unsigned long long>(bands.lte_mask),
                      static_cast<unsigned long long>(bands.tdscdma_mask),
                      static_cast<unsigned long long>(bands.lte_mask_over_64));
    }

    modem::RegistrationInfo reg_info;
    status = modem.get_registration_status(reg_info);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve registration status");
        log_status(status);
    } else {
        MODEM_LOG_INF("Registration Status: %d", static_cast<int>(reg_info.stat));
        MODEM_LOG_INF("Location Area Code: %s", reg_info.lac.c_str());
        MODEM_LOG_INF("Cell ID: %s", reg_info.ci.c_str());
        MODEM_LOG_INF("Access Technology: %d", static_cast<int>(reg_info.act));
        MODEM_LOG_INF("Has Location: %s", reg_info.fHasLocation ? "Yes" : "No");
    }

    modem::SignalQuality sq;
    status = modem.get_signal_quality(sq);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve signal quality");
        log_status(status);
    } else {
        MODEM_LOG_INF("Signal Quality - RSSI: %d, BER: %d, RSRQ: %d, RSRP: %d", sq.rssi, sq.ber, sq.rsrq, sq.rsrp);
    }

    modem::FixedString<modem::MODEM_MEDIUM_STR> oper;
    status = modem.get_operator(oper);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve operator");
        log_status(status);
    } else {
        MODEM_LOG_INF("Operator: %s", oper.c_str());
    }

    modem::FixedString<modem::MODEM_MEDIUM_STR> apn;
    uint8_t cid = 1; // Example CID for PDP context
    status = modem.get_apn(cid, apn);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve APN");
        log_status(status);
    } else {
        MODEM_LOG_INF("APN: %s", apn.c_str());
    }

    bool fActive = false;
    status = modem.get_pdp_state(cid, fActive);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve PDP state");
        log_status(status);
    } else {
        MODEM_LOG_INF("PDP Context %d is %s", cid, fActive ? "Active" : "Inactive");
    }

    modem::FixedString<modem::MODEM_IP_STR> ip_addr;
    status = modem.get_ip_address(cid, ip_addr);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IP address");
        log_status(status);
    } else {
        MODEM_LOG_INF("IP Address for PDP Context %d: %s", cid, ip_addr.c_str());
    }

    modem::RadioTech tech = modem::RadioTech::cat_m1;
    uint8_t gsmPriority = 0;
    status = modem.get_iot_tech(tech, gsmPriority);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IoT technology");
        log_status(status);
    } else {
        switch (static_cast<int>(tech)) {
            case 0: MODEM_LOG_INF("IoT Technology: CAT-M1"); break;
            case 1: MODEM_LOG_INF("IoT Technology: NB-IoT"); break;
            case 2: MODEM_LOG_INF("IoT Technology: CAT-M1 preferred + NB-IoT"); break;
            case 3: MODEM_LOG_INF("IoT Technology: CAT-M1 + NB-IoT preferred"); break;
            default: MODEM_LOG_INF("IoT Technology: Unknown (%d)", static_cast<int>(tech)); break;
        }
    }

    modem::NetworkSurveyResult survey_result;
    status = modem.network_survey(survey_result);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Network survey failed");
        log_status(status);
    } else {
        MODEM_LOG_INF("Network survey completed, found %zu cells", survey_result.cells.size());
        for (const auto& cell : survey_result.cells) {
            MODEM_LOG_INF("Cell: Type=%d, ARFCN=%d, RSRP=%.1f dBm", static_cast<int>(cell.type), cell.arfcn, cell.rsrp);
        }
    }

    controller.disconnect();
    return 0;
}