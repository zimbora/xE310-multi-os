#pragma once

#include "modem/modem_controller.h"

#include <cstdint>
#include <string>

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

/// Radio access technology for AT#WS46 / AT+COPS.
enum class RadioTech : uint8_t {
    //lte = 7,
    cat_m1 = 8,
    nb_iot = 9,
};

/// Network registration status.
enum class RegStatus : uint8_t {
    not_registered = 0,
    registered_home = 1,
    searching = 2,
    denied = 3,
    unknown = 4,
    registered_roaming = 5,
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

    // --- Network ---

    /// AT+CGDCONT — Set PDP context (APN).
    ModemStatus set_apn(uint8_t cid, const std::string& apn);

    /// AT+CGDCONT? — Read current APN for a context.
    ModemStatus get_apn(uint8_t cid, std::string& apn);

    /// AT#WS46 — Set radio access technology.
    ModemStatus set_radio_tech(RadioTech tech);

    /// AT#BND — Set LTE band bitmask.
    ModemStatus set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask);

    /// AT#BND? — Read current band configuration.
    ModemStatus get_bands(std::string& bands);

    /// AT+CEREG? — Query EPS network registration status.
    ModemStatus get_registration_status(RegStatus& status);

    /// AT+CSQ — Query signal quality (RSSI).
    ModemStatus get_signal_quality(int& rssi, int& ber);

    /// AT+COPS? — Read current operator.
    ModemStatus get_operator(std::string& oper);

    /// AT+COPS=0 — Automatic operator selection.
    ModemStatus set_operator_auto();

private:
    ModemController& controller_;
};

} // namespace modem
