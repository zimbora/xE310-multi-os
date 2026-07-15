#pragma once

#include "modem/modem_controller.h"

#include <cstdint>
#include <string_view>

#define ME310M1

namespace modem {

/// Buffer sizes for modem string fields.
static constexpr size_t MODEM_SHORT_STR = 32;  ///< IMEI, ICCID, IMSI, etc.
static constexpr size_t MODEM_MEDIUM_STR = 64; ///< APN, operator names, etc.
static constexpr size_t MODEM_LONG_STR = 128;  ///< Version strings, identifications
static constexpr size_t MODEM_IP_STR = 48;     ///< IP addresses (IPv6 max)

/// SIM detection mode for AT#SIMDET.
enum class SimDetMode : uint8_t {
    gpio = 0,   ///< SIM detection via GPIO
    always = 1, ///< SIM always inserted
};

/// SIM status for AT#QSS.
enum class SimStatus : uint8_t {
    not_inserted = 0,
    inserted = 1,
    inserted_and_pin_unlocked = 2,
    inserted_and_ready = 3,
};

/// Radio access technology for AT+COPS <act> parameter.
enum class RadioTech : int8_t {
    unknown = -1,
    gsm = 0,
    lte = 6,
    cat_m1 = 7,
    nb_iot = 9,
};

/// RAT selector for AT%TRSHCMD="BSPFILE","ERASE_LTEPP",<param>.
enum class MruListRat : uint8_t {
    lte_m = 0,
    nb_iot = 2,
    ntn = 5,
};

/// Network registration status for AT+CEREG.
enum class RegStatus : uint8_t {
    not_registered = 0,
    registered_home = 1,
    searching = 2,
    denied = 3,
    unknown = 4, // out of coverage or suspended. SIM in PSM/eDRX can also show as unknown.
    registered_roaming = 5,
    sms_only = 6,
    sms_only_roaming = 7,
    emergency = 8,
    csfb_home = 9,
    csfb_roaming = 10,
};

/// Full registration info from AT+CEREG?
struct RegistrationInfo {
    FixedString<MODEM_SHORT_STR> lac;            ///< TAC (tracking area code) for LTE / LAC for 2G
    FixedString<MODEM_SHORT_STR> ci;             ///< Cell identity
    FixedString<MODEM_SHORT_STR> active_time;    ///< T3324 active timer (encoded bit string, e.g. "01100000")
    FixedString<MODEM_SHORT_STR> periodic_tau;   ///< T3412 periodic TAU timer (encoded bit string, e.g. "01000011")
    FixedString<MODEM_IP_STR> ip_address;        ///< IP address assigned to the modem (from AT+CGDCONT?)
    FixedString<MODEM_MEDIUM_STR> operator_name; ///< Operator name from AT+COPS?
    FixedString<MODEM_MEDIUM_STR> apn;           ///< APN from AT+CGDCONT?
    uint8_t mode = 0; ///< Registration mode (0=automatic, 1=manual, 2=deregistered, 3=manual/automatic)
    RegStatus stat = RegStatus::not_registered; ///< Registration status (0-10)
    RadioTech act = RadioTech::gsm;             ///< Access technology (0=GSM, 6=LTE, 7=Cat-M1, 9=NB-IoT)
    bool fHasLocation = false;                  ///< Flag indicating if lac and ci are valid
    uint8_t cause_type = 0;                     ///< Cause type for registration rejection
    uint8_t reject_cause = 0;                   ///< Reject cause value
    bool fHasReject = false;                    ///< Flag indicating if reject cause is valid
    bool fHasPsm = false;                       ///< Flag indicating if PSM info is valid
};

enum class ContextState : uint8_t {
    inactive = 0,
    active = 1,
};

struct NetworkInfo {
    ContextState context_state = ContextState::inactive;
    FixedString<MODEM_IP_STR> ip_address;
};

/// Signal quality from AT+CESQ.
struct SignalQuality {
    int rssi = 99;  ///< AT+CESQ rxlev: 0-63 (-110..-47 dBm step 1), 99=unknown (2G/GERAN)
    int ber = 99;   ///< AT+CESQ ber:   0-7 bit error rate class, 99=unknown (2G)
    int rsrq = 255; ///< AT+CESQ rsrq:  0-34, 255=unknown. dBm = -19.5 + val*0.5  (LTE)
    int rsrp = 255; ///< AT+CESQ rsrp:  0-97, 255=unknown. dBm = -140  + val       (LTE)

    /// Convert raw rsrp to dBm. Returns INT_MIN if unknown (255).
    int rsrp_dbm() const { return rsrp == 255 ? -999 : -140 + rsrp; }
    /// Convert raw rsrq to tenths of dB (e.g. -175 = -17.5 dB). Returns INT_MIN if unknown.
    int rsrq_tenth_db() const { return rsrq == 255 ? -9999 : -195 + (rsrq * 5); }
};

/// PSM mode for AT+CPSMS and AT#CPSMS.
enum class PsmMode : uint8_t {
    disable = 0,
    enable = 1,
};

enum class PsmVersion : uint8_t {
    no_coord = 1,        ///< PSM without network coordination (legacy)
    rel12_no_retain = 2, ///< Rel-12 PSM without context retain support
    rel12_retain = 4,    ///< Rel-12 PSM with context retain support
    edrx = 8,            ///< eDRX (extended discontinuous reception) mode
};

/// AT+CPSMS configuration (3GPP standard — timer values as 8-bit binary octet strings).
struct CpsmsConfig {
    PsmMode mode = PsmMode::disable;
    FixedString<MODEM_SHORT_STR> req_periodic_rau;     ///< T3312 octet string, e.g. "01000111" (GERAN)
    FixedString<MODEM_SHORT_STR> req_gprs_ready_timer; ///< T3314 octet string (GERAN)
    FixedString<MODEM_SHORT_STR> req_periodic_tau;     ///< T3412 octet string, e.g. "10101100"
    FixedString<MODEM_SHORT_STR> req_active_time;      ///< T3324 octet string, e.g. "00100010"
};

/// AT#CPSMS set configuration (Telit-specific — timer values in seconds as integers).
struct TelitCpsmsConfig {
    PsmMode mode = PsmMode::disable;
    bool fHasPeriodicRau = false;
    uint32_t req_periodic_rau = 0; ///< T3312 in seconds (GERAN)
    bool fHasGprsReadyTimer = false;
    uint32_t req_gprs_ready_timer = 0; ///< T3314 in seconds (GERAN)
    bool fHasPeriodicTau = false;
    uint32_t req_periodic_tau = 0; ///< T3412 in seconds
    bool fHasActiveTime = false;
    uint32_t req_active_time = 0; ///< T3324 in seconds
    bool fHasPsmVersion = false;
    PsmVersion psm_version =
        PsmVersion::rel12_retain; ///< bitmask: 1=no-coord, 2=Rel12 no-retain, 4=Rel12 retain, 8=eDRX
    bool fHasPsmThreshold = false;
    uint32_t psm_threshold = 60; ///< min duration threshold to enter PSM, seconds (min 60)
};

/// AT#CPSMS? read response.
struct TelitCpsmsStatus {
    uint8_t status = 0; ///< 0=PSM disabled in the network, 1=PSM enabled in the network
    uint32_t t3324 = 0; ///< active time granted by network, seconds
    uint32_t t3412 = 0; ///< TAU timer granted by network, seconds
    uint8_t psm_version = 0;
    uint32_t psm_threshold = 0;
    PsmMode mode = PsmMode::disable;
};

/// Technology type of a network survey cell entry.
enum class SurvCellType : uint8_t {
    cell_2g_bcch,     ///< 2G BCCH carrier (full info)
    cell_2g_non_bcch, ///< 2G non-BCCH carrier (arfcn + rxLev only)
    cell_4g,          ///< 4G/LTE cell
};

/// A single cell entry from AT#CSURVC.
struct SurvCell {
    SurvCellType type = SurvCellType::cell_4g;

    // --- 2G BCCH ---
    int arfcn = 0;
    int bsic = 0;
    int rx_lev = 0; ///< dBm
    int ber = 0;
    uint16_t mcc = 0;
    uint16_t mnc = 0;
    uint32_t lac = 0;
    uint32_t cell_id = 0;
    FixedString<MODEM_SHORT_STR> cell_stat; ///< CELL_SUITABLE, CELL_BARRED, etc.
    int num_arfcn = 0;

    // --- 4G ---
    int earfcn = 0;
    uint32_t tac = 0;
    uint32_t phys_cell_id = 0;
    uint64_t cell_identity = 0;
    float rsrp = 0.0F;
    float rsrq = 0.0F;
};

/// Result of AT#CSURVC.
struct NetworkSurveyResult {
    static constexpr size_t MAX_CELLS = 32;
    StaticVector<SurvCell, MAX_CELLS> cells;
    bool fHasSummary = false;
    int no_arfcn = 0; ///< total scanned frequencies (if #CSURVF=2)
    int no_bcch = 0;  ///< found BCCH (if #CSURVF=2)
};

/// Software package version from AT#SWPKGV.
struct SoftwarePackageVersion {
    FixedString<MODEM_LONG_STR> package_version; ///< <Telit Software Package Version>-<Production Parameters Version>
    FixedString<MODEM_LONG_STR> modem_version;   ///< <Modem Package Version>
    FixedString<MODEM_LONG_STR> prod_params_version; ///< <Production Parameters Version>
    FixedString<MODEM_LONG_STR> app_version;         ///< <Application Software Version>
};

struct ModemInfo {
    FixedString<MODEM_SHORT_STR> imei_sv; ///< IMEI Software Version from AT+IMEISV
    FixedString<MODEM_SHORT_STR> iccid;   ///< ICCID from AT+CCID
    FixedString<MODEM_SHORT_STR> imsi;    ///< IMSI from AT+CIMI
    FixedString<MODEM_MEDIUM_STR> model_id;
    SoftwarePackageVersion sw_package_version;
    FixedString<MODEM_MEDIUM_STR> telit_id;
    FixedString<MODEM_LONG_STR> identification;
    FixedString<MODEM_SHORT_STR> imei;
};

/// Operator info from AT+COPS?.
struct Operator {
    FixedString<MODEM_MEDIUM_STR> long_name;
    FixedString<MODEM_MEDIUM_STR> short_name;
    FixedString<MODEM_SHORT_STR> numeric;
    RadioTech act = RadioTech::unknown;
};

/// A single cell entry from AT#CSURV with AT#CSURVF=2 (hex numeric format).
struct CsurvCell {
    int earfcn = 0;             ///< E-UTRA Assigned Radio Channel
    int rx_lev = 0;             ///< Reception level in dBm
    uint16_t mcc = 0;           ///< Mobile country code (hex)
    uint16_t mnc = 0;           ///< Mobile network code (hex)
    uint32_t cell_id = 0;       ///< Physical cell identifier (hex)
    uint32_t tac = 0;           ///< Tracking Area Code (4-digit hex)
    uint64_t cell_identity = 0; ///< Cell identifier (hex)
};

/// Result of AT#CSURV (with AT#CSURVF=2 pre-set).
struct CsurvResult {
    static constexpr size_t MAX_CELLS = 32;
    StaticVector<CsurvCell, MAX_CELLS> cells;
    bool fHasSummary = false;
    int no_arfcn = 0; ///< Number of scanned frequencies
    int no_bcch = 0;  ///< Number of found BCCH
};

/// AT#BND bitmask configuration.
struct BandConfig {
    uint64_t gsm_mask = 0;
    uint64_t umts_mask = 0;
    uint64_t lte_mask = 0;
    uint64_t tdscdma_mask = 0;
    uint64_t lte_mask_over_64 = 0;
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
    ModemStatus request_imei_sv(FixedString<MODEM_SHORT_STR>& imei_sv);

    /// AT#CGMM — Request Model Identification.
    ModemStatus request_model_id(FixedString<MODEM_MEDIUM_STR>& model);

    /// AT#SWPKGV — Request Software Package Version.
    ModemStatus request_sw_package_version(SoftwarePackageVersion& ver);

    /// AT#TID — Request Telit ID.
    ModemStatus request_telit_id(FixedString<MODEM_MEDIUM_STR>& tid);

    /// ATI — Request Identification Information.
    ModemStatus request_identification(FixedString<MODEM_LONG_STR>& info);

    /// AT+CGSN — Request IMEI (Product Serial Number).
    ModemStatus get_imei(FixedString<MODEM_SHORT_STR>& imei);

    /// AT+CCLK? — Read modem clock.
    ModemStatus get_clock(FixedString<MODEM_SHORT_STR>& clock);

    // --- SIM Card ---

    /// AT#CCID — Read SIM ICCID.
    ModemStatus read_iccid(FixedString<MODEM_SHORT_STR>& iccid);

    /// AT+CIMI — Read SIM IMSI.
    ModemStatus read_imsi(FixedString<MODEM_SHORT_STR>& imsi);

    /// AT#SIMDET — Set SIM detection mode.
    ModemStatus set_sim_detection(SimDetMode mode);

    /// AT#QSS — Query SIM status.
    ModemStatus query_sim_status(SimStatus& status);

    /// AT+CSIM — Send a command to the SIM card.
    ModemStatus send_sim_command(std::string_view command, FixedString<AT_RESPONSE_MAX>& sim_response);

    // --- PSM ---

    /// AT+CPSMS — Set PSM parameters (3GPP standard, timer values as binary octet strings).
    ModemStatus set_psm(const CpsmsConfig& cfg);

    /// AT+CPSMS? — Read current PSM configuration.
    ModemStatus get_psm(CpsmsConfig& cfg);

    /// AT+CPSMS= — Disable PSM and reset all parameters to defaults.
    ModemStatus disable_psm();

    /// AT#CPSMS — Set PSM parameters (Telit-specific, timer values in seconds).
    ModemStatus set_telit_psm(const TelitCpsmsConfig& cfg);

    /// AT#CPSMS? — Read current PSM status as reported by the network.
    ModemStatus get_telit_psm(TelitCpsmsStatus& status);

    /// AT#CPSMS= — Disable Telit PSM and reset all parameters to defaults.
    ModemStatus disable_telit_psm();

    /// AT#PSMURC — Enable/disable the PSM entry URC (#PSMURC=<ActiveTime>,<PSMTime>).
    ModemStatus set_psm_urc(bool enable);

    /// AT#PSMURC? — Read PSM URC enable state.
    ModemStatus get_psm_urc(bool& enabled);

    // --- Power ---

    /// Power on the module (hardware power-up sequence).
    static void power_on();

    /// Power off the module (hardware power-down sequence).
    static void power_off();

    /// AT+CFUN=1 — Power on the radio (full functionality).
    ModemStatus power_radio();

    /// AT+CFUN=0 — Power off the radio (minimum functionality).
    ModemStatus power_off_radio();

    /// AT#SHDN — Software shutdown. Detaches from network and powers off the module.
    ModemStatus shutdown();

    /// AT#REBOOT — Immediately reboot the module.
    ModemStatus reboot();

    // --- Network Registration ---

    // AT+CEREG=2 — Enable network registration URC with location info and IP address.
    ModemStatus set_registration_urc(bool enable);

    /// AT%TRSHCMD="BSPFILE","ERASE_LTEPP",<param> — Delete MRU list for selected RAT.
    ModemStatus delete_mru_list(MruListRat rat);

    /// AT#CSURVC — Network survey (numeric format). Scans all channels in the current band.
    /// Optionally restrict to channels [start_ch, end_ch]. Pass 0 for both to scan full band.
    ModemStatus network_survey(NetworkSurveyResult& result, uint32_t start_ch = 0, uint32_t end_ch = 0);

    /// call set bands
    ModemStatus set_lte_bands(uint64_t lte_mask) { return set_bands(0, 0, lte_mask); }

    /// AT#BND — Set band bitmasks.
    ModemStatus set_bands(uint64_t gsm_mask, uint64_t umts_mask, uint64_t lte_mask, uint64_t tdscdma_mask = 0,
                          uint64_t lte_mask_over_64 = 0);

    /// AT#BND? — Read current band configuration.
    ModemStatus get_bands(BandConfig& bands);

    /// AT#WS46 — Select IoT technology (takes effect after reboot).
    /// <n>: 0=CAT-M1, 1=NB-IoT, 2=CAT-M1 preferred+NB-IoT, 3=CAT-M1+NB-IoT preferred.
    /// <gsm_priority>: 0=LTE priority, 1=GSM priority.
    ModemStatus set_iot_tech(RadioTech tech, uint8_t gsm_priority = 0);

    /// AT#WS46? — Read currently selected IoT technology and priority.
    ModemStatus get_iot_tech(RadioTech& tech, uint8_t& gsm_priority);

    /// AT+CEREG? — Query EPS network registration status.
    ModemStatus get_registration_status(RegistrationInfo& info, RadioTech tech = RadioTech::gsm);

    /// AT+CGATT=1 — Attach to packet domain network.
    ModemStatus network_attach();

    /// AT+CGATT=0 — Detach from packet domain network.
    ModemStatus network_detach();

    /// AT+CESQ — Query extended signal quality.
    ModemStatus get_signal_quality(SignalQuality& sq);

    /// AT+COPS — Set radio access technology.
    ModemStatus set_radio_tech(RadioTech tech);

    /// AT+COPS — Manual operator selection with access technology and automatic fallback.
    ModemStatus set_operator_manual(std::string_view oper, RadioTech tech);

    /// AT+COPS=0 — Automatic operator selection.
    ModemStatus set_operator_auto();

    /// AT+COPS? — Read current operator. Parses +COPS: <mode>[,<format>,<oper>,<act>] into oper.
    ModemStatus get_operator(FixedString<MODEM_MEDIUM_STR>& oper);

    /// AT+COPS=? — List all available operators.
    /// Parses +COPS: (<stat>,<long>,<short>,<numeric>,<act>),... into operators.
    static constexpr size_t MAX_OPERATORS = 16;
    ModemStatus get_available_operators(StaticVector<Operator, MAX_OPERATORS>& operators);

    /// AT#CSURVF=2 + AT#CSURV — Network survey in labeled/hex format.
    /// Sets CSURVF=2 (hex numerics + summary line) then runs AT#CSURV[=<s>,<e>].
    /// Pass 0 for both start_ch and end_ch to scan the full band.
    ModemStatus scan_networks(CsurvResult& result, uint32_t start_ch = 0, uint32_t end_ch = 0);

    // --- Network Attach ---

    /// AT+CGDCONT — Set PDP context (APN).
    ModemStatus set_apn(uint8_t cid, std::string_view apn);

    /// AT+CGDCONT? — Read current APN and IP address for a context.
    /// Parses +CGDCONT: <cid>,<PDP_type>,<APN>,<PDP_addr>,... and gets apn.
    ModemStatus get_apn(uint8_t cid, FixedString<MODEM_MEDIUM_STR>& apn);

    /// AT+CGEREP — Set command enables/disables sending of unsolicited result codes in case of certain events
    /// occurring in the module or in the network.
    ModemStatus set_pdp_urc(bool enable);

    /// AT%NOTIFYEV="PLMNSEARCHEXH",<n> — Enable/disable PLMNSEARCHEXH notifications.
    ModemStatus set_plmnsearchexh_notify(bool enable);

    /// AT+CGACT=1 — Activate PDP context.
    ModemStatus activate_pdp(uint8_t cid);

    /// AT+CGACT=0 — Deactivate PDP context.
    ModemStatus deactivate_pdp(uint8_t cid);

    /// AT+CGACT? — Query PDP context activation state (0=deactivated, 1=activated).
    ModemStatus get_pdp_state(uint8_t cid, bool& active);

    /// AT+CGPADDR — Get the IP address for a PDP context.
    ModemStatus get_ip_address(uint8_t cid, FixedString<MODEM_IP_STR>& ip_addr);

    /// AT+CGCONTRDP — Get full PDP context dynamic parameters (IP, gateway, DNS).
    ModemStatus get_pdp_info(uint8_t cid, FixedString<MODEM_IP_STR>& ip_addr, FixedString<MODEM_IP_STR>& gw_addr,
                             FixedString<MODEM_IP_STR>& dns_primary, FixedString<MODEM_IP_STR>& dns_secondary);

    // --- UDP Connection ---

    /// AT#SD — Open a UDP socket to a remote host.
    /// AT#SD=<connId>,1,<rPort>,"<host>",0,<lPort>,1
    ModemStatus udp_open(uint8_t conn_id, std::string_view host, uint16_t remote_port, uint16_t local_port = 0);

    /// AT#SL — Listen for incoming UDP data on a local port.
    ModemStatus udp_listen(uint8_t conn_id, uint16_t local_port, uint8_t cid = 1);

    /// AT#SSEND / AT#SSENDEXT — Send data over an open UDP socket.
    ModemStatus udp_send(uint8_t conn_id, const uint8_t* data, size_t length);

    /// AT#SRECV — Receive data from a UDP socket.
    static constexpr size_t UDP_MAX_BYTES = 1500;
    ModemStatus udp_receive(uint8_t conn_id, StaticVector<uint8_t, UDP_MAX_BYTES>& data, uint16_t max_bytes = 1500);

    /// AT#SH — Close a UDP socket.
    ModemStatus udp_close(uint8_t conn_id);

    /// AT#SS — Query socket status.
    ModemStatus udp_status(uint8_t conn_id, uint8_t& state);

    /// Send a raw AT command string and return the response body.
    ModemStatus send_at_command(std::string_view command, FixedString<AT_RESPONSE_MAX>& response,
                                uint32_t timeout_ms = 5000);

    // --- Event Handlers ---

    /// Returns the last registration info populated by parse_urc_handler or get_registration_status.
    const RegistrationInfo& registration_info() const;

    /// Returns the last ModemStatus stored after a command execution.
    ModemStatus last_status() const;

    /// Poll the UART for any pending URC lines (non-blocking, max timeout_ms wait).
    StaticVector<FixedString<URC_LINE_MAX>, ModemController::MAX_URC_LINES> poll_urc(uint32_t timeout_ms = 10);

private:
    ModemController& controller_;

    RegistrationInfo info;
    TelitCpsmsStatus psm_status;

    ModemStatus last_status_ = ModemStatus::ok; ///< Last AT command status

    /// Internal helper: sends a raw AT command, stores the result in last_status_, and returns it.
    ModemStatus send_raw(std::string_view command, AtResponse& response, uint32_t timeout_ms = 5000, bool retry = true);

    std::unique_ptr<TimerInterface> cmd_timer_; ///< Timer for at commands responses
};

} // namespace modem
