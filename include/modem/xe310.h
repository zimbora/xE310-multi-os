#pragma once

#include "modem/modem_controller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace modem {

/// SIM detection mode for AT#SIMDET.
enum class SimDetMode : uint8_t {
    gpio = 0,     ///< SIM detection via GPIO
    always = 1,   ///< SIM always inserted
};

/// SIM status for AT#QSS.
enum class SimStatus : uint8_t {
    not_inserted = 0,
    inserted = 1,
    inserted_and_pin_unlocked = 2,
    inserted_and_ready = 3,
};

/// Radio access technology for AT+COPS <act> parameter.
enum class RadioTech : uint8_t {
    gsm = 0,
    lte = 7,
    cat_m1 = 8,
    nb_iot = 9,
};

/// Network registration status for AT+CEREG.
enum class RegStatus : uint8_t {
    not_registered = 0,
    registered_home = 1,
    searching = 2,
    denied = 3,
    unknown = 4,
    registered_roaming = 5,
};

/// Signal quality from AT+CESQ.
struct SignalQuality {
    int rssi = 99;    ///< 0-31 (-113..-51 dBm), 99=unknown (2G)
    int ber = 99;     ///< 0-7 bit error rate, 99=unknown (2G)
    int rsrq = 255;   ///< 0-34 ref signal quality, 255=unknown (LTE)
    int rsrp = 255;   ///< 0-97 ref signal power, 255=unknown (LTE)
};

/// Telit ME310 modem — wraps ModemController with ME310-specific commands.
class xE310 {
public:
    explicit xE310(ModemController& controller);

    /// AT+IPR — Set UART baud rate.
    ModemStatus set_baudrate(uint32_t baudrate);

    /// ATE — Set command echo (0 = off, 1 = on).
    ModemStatus set_echo(bool enable);

    /// AT — Check if modem is responsive.
    ModemStatus at_ok();

    /// AT+IMEISV — Request IMEI and Software Version.
    ModemStatus request_imei_sv(std::string& imei_sv);

    /// AT#CGMM — Request Model Identification.
    ModemStatus request_model_id(std::string& model);

    /// AT#TID — Request Telit ID.
    ModemStatus request_telit_id(std::string& tid);

    /// ATI — Request Identification Information.
    ModemStatus request_identification(std::string& info);

    // --- SIM Card ---

    /// AT+CCID — Read SIM ICCID.
    ModemStatus read_iccid(std::string& iccid);

    /// AT+CIMI — Read SIM IMSI.
    ModemStatus read_imsi(std::string& imsi);

    /// AT#SIMDET — Set SIM detection mode.
    ModemStatus set_sim_detection(SimDetMode mode);

    /// AT#QSS — Query SIM status.
    ModemStatus query_sim_status(SimStatus& status);

    /// AT+CSIM — Send a command to the SIM card.
    ModemStatus send_sim_command(const std::string& command, std::string& sim_response);

    // --- Network Registration ---

    /// AT#BND — Set band bitmasks.
    ModemStatus set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask,
                          uint64_t tdscdma_mask = 0, uint64_t lte_mask_over_64 = 0);

    /// AT#BND? — Read current band configuration.
    ModemStatus get_bands(std::string& bands);

    /// AT+CEREG? — Query EPS network registration status.
    ModemStatus get_registration_status(RegStatus& status);

    /// AT+CESQ — Query extended signal quality.
    ModemStatus get_signal_quality(SignalQuality& sq);

    /// AT+COPS — Set radio access technology.
    ModemStatus set_radio_tech(RadioTech tech);

    /// AT+COPS — Manual operator selection with access technology and automatic fallback.
    ModemStatus set_operator_manual(const std::string& oper, RadioTech tech);

    /// AT+COPS=0 — Automatic operator selection.
    ModemStatus set_operator_auto();

    /// AT+COPS? — Read current operator.
    ModemStatus get_operator(std::string& oper);

    // --- Network Attach ---

    /// AT+CGDCONT — Set PDP context (APN).
    ModemStatus set_apn(uint8_t cid, const std::string& apn);

    /// AT+CGDCONT? — Read current APN for a context.
    ModemStatus get_apn(uint8_t cid, std::string& apn);

    /// AT+CGACT=1 — Activate PDP context.
    ModemStatus activate_pdp(uint8_t cid);

    /// AT+CGACT=0 — Deactivate PDP context.
    ModemStatus deactivate_pdp(uint8_t cid);

    /// AT+CGACT? — Query PDP context activation state (0=deactivated, 1=activated).
    ModemStatus get_pdp_state(uint8_t cid, bool& active);

    /// AT+CGPADDR — Get the IP address for a PDP context.
    ModemStatus get_ip_address(uint8_t cid, std::string& ip_addr);

    /// AT+CGCONTRDP — Get full PDP context dynamic parameters (IP, gateway, DNS).
    ModemStatus get_pdp_info(uint8_t cid, std::string& ip_addr, std::string& gw_addr,
                             std::string& dns_primary, std::string& dns_secondary);

    // --- UDP Connection ---

    /// AT#SD — Open a UDP socket to a remote host.
    ModemStatus udp_open(uint8_t conn_id, const std::string& host, uint16_t remote_port,
                         uint16_t local_port = 0, uint8_t cid = 1);

    /// AT#SL — Listen for incoming UDP data on a local port.
    ModemStatus udp_listen(uint8_t conn_id, uint16_t local_port, uint8_t cid = 1);

    /// AT#SSEND / AT#SSENDEXT — Send data over an open UDP socket.
    ModemStatus udp_send(uint8_t conn_id, const std::vector<uint8_t>& data);

    /// AT#SRECV — Receive data from a UDP socket.
    ModemStatus udp_receive(uint8_t conn_id, std::vector<uint8_t>& data, uint16_t max_bytes = 1500);

    /// AT#SH — Close a UDP socket.
    ModemStatus udp_close(uint8_t conn_id);

    /// AT#SS — Query socket status.
    ModemStatus udp_status(uint8_t conn_id, uint8_t& state);

private:
    ModemController& controller_;
};

} // namespace modem
