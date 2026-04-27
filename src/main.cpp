#include "modem/modem_controller.h"
#include "modem/at_command.h"
#include "modem/uart_factory.h"
#include "modem/xe310.h"
#include "modem/log.h"
#include <memory>

MODEM_LOG_MODULE_REGISTER(modem_app);

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
        return 1;
    }

    modem::xE310 modem(controller);

    status = modem.at_ok();
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Modem not responsive");
        controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Modem initialized successfully");
    status = modem.set_echo(false);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to disable echo");
    } else {
        MODEM_LOG_INF("Echo disabled");
    }

    std::string imei_sv;
    status = modem.request_imei_sv(imei_sv);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IMEI/SV");
    } else {
        MODEM_LOG_INF("IMEI/SV: %s", imei_sv.c_str());
    }

    std::string request_model_id;
    status = modem.request_model_id(request_model_id);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve model ID");
    } else {
        MODEM_LOG_INF("Model ID: %s", request_model_id.c_str());
    }

    modem::SoftwarePackageVersion ver;
    status = modem.request_sw_package_version(ver);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve software package version");
    } else {
        MODEM_LOG_INF("Software Package Version: %s", ver.package_version.c_str());
        MODEM_LOG_INF("Modem Version: %s", ver.modem_version.c_str());
        MODEM_LOG_INF("Production Parameters Version: %s", ver.prod_params_version.c_str());
        MODEM_LOG_INF("Application Software Version: %s", ver.app_version.c_str());
    }

    std::string read_iccid;
    status = modem.read_iccid(read_iccid);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve ICCID");
    } else {
        MODEM_LOG_INF("ICCID: %s", read_iccid.c_str());
    }

    std::string read_imsi;
    status = modem.read_imsi(read_imsi);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IMSI");
    } else {
        MODEM_LOG_INF("IMSI: %s", read_imsi.c_str());
    }
    
    modem::TelitCpsmsStatus psm_status;
    status = modem.get_telit_psm(psm_status);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve PSM status");
    } else {
        MODEM_LOG_INF("PSM Status: %d", psm_status.status);
        MODEM_LOG_INF("T3324: %u seconds", psm_status.t3324);
        MODEM_LOG_INF("T3412: %u seconds", psm_status.t3412);
        MODEM_LOG_INF("PSM Version: %d", psm_status.psm_version);
        MODEM_LOG_INF("PSM Threshold: %u seconds", psm_status.psm_threshold);
        MODEM_LOG_INF("PSM Mode: %d", static_cast<int>(psm_status.mode));
    }

    std::string bands;
    status = modem.get_bands(bands);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve band configuration");
    } else {
        MODEM_LOG_INF("Band Configuration: %s", bands.c_str());
    }   

    modem::RegistrationInfo reg_info;
    status = modem.get_registration_status(reg_info);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve registration status");
    } else {
        MODEM_LOG_INF("Registration Status: %d", static_cast<int>(reg_info.stat));
        MODEM_LOG_INF("Location Area Code: %s", reg_info.lac.c_str());
        MODEM_LOG_INF("Cell ID: %s", reg_info.ci.c_str());
        MODEM_LOG_INF("Access Technology: %d", static_cast<int>(reg_info.act));
        MODEM_LOG_INF("Has Location: %s", reg_info.has_location ? "Yes" : "No");
    }   

    modem::SignalQuality sq;
    status = modem.get_signal_quality(sq);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve signal quality");
    } else {
        MODEM_LOG_INF("Signal Quality - RSSI: %d, BER: %d, RSRQ: %d, RSRP: %d",
                      sq.rssi, sq.ber, sq.rsrq, sq.rsrp);
    }   

    std::string oper;
    status = modem.get_operator(oper);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve operator");
    } else {
        MODEM_LOG_INF("Operator: %s", oper.c_str());
    }   

    std::string apn;
    uint8_t cid = 1; // Example CID for PDP context
    status = modem.get_apn(cid, apn);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve APN");
    } else {
        MODEM_LOG_INF("APN: %s", apn.c_str());
    }   

    bool active = false;
    status = modem.get_pdp_state(cid, active);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve PDP state");
    } else {
        MODEM_LOG_INF("PDP Context %d is %s", cid, active ? "Active" : "Inactive");
    }

    std::string ip_addr;
    status = modem.get_ip_address(cid, ip_addr);
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to retrieve IP address");
    } else {
        MODEM_LOG_INF("IP Address for PDP Context %d: %s", cid, ip_addr.c_str());
    }
    
    controller.disconnect();
    return 0;
}