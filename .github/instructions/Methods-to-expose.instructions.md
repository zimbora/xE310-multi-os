---
description: This file describes the methods to expose for retrieving the last known state of the modem, including registration info, signal quality, SIM details, network info, and configuration.
applyTo: when agent isolatedEnv is running
---

<!-- Tip: Use /create-instructions in chat to generate content with agent assistance -->
Expose methods to retrieve the last known state of the modem, including registration info, signal quality, SIM details, network info, and configuration. These methods provide access to the most recent data read from the modem and allow for scanning networks and managing server connections.

Create file i_radio_lte to expose it through events and messaages.

    /// Last registration info read from the modem.
    const RegistrationInfo& registration_info() const;

    /// Last signal quality read from the modem.
    const SignalQuality& signal_quality() const;

    /// SIM ICCID read at power-on.
    const FixedString<MODEM_SHORT_STR>& iccid() const;

    /// SIM IMSI read at power-on.
    const FixedString<MODEM_SHORT_STR>& imsi() const;

    /// Full modem identification info read at power-on.
    const ModemInfo& modem_info() const;

    /// Last known SIM status.
    SimStatus sim_status() const;

    /// Last known radio access technology.
    RadioTech radio_tech() const;

    /// Last known registration status (from URC or query).
    RegStatus reg_status() const;

    /// Last known network/PDP context info.
    const NetworkInfo& network_info() const;

    /// Last known PSM mode.
    PsmMode psm_mode() const;

    /// Last known 3GPP PSM configuration.
    const CpsmsConfig& cpsms_config() const;

    /// Last known Telit PSM configuration.
    const TelitCpsmsConfig& telit_cpsms_config() const;

    /// Last known Telit PSM network status.
    const TelitCpsmsStatus& telit_cpsms_status() const;

    /// Last network survey result (populated after a survey action).
    const NetworkSurveyResult& network_survey_result() const;

    /// List of operators found by the last AT+COPS=? scan.
    const StaticVector<Operator, xE310::MAX_OPERATORS>& available_operators() const;

    /// Result of the last AT#CSURV scan (populated by scan_networks()).
    const CsurvResult& csurv_result() const;

    /// Run AT#CSURVF=2 + AT#CSURV and store results internally.
    /// Optionally restrict to channels [start_ch, end_ch]; pass 0 for both to scan full band.
    bool scan_networks(uint32_t start_ch = 0, uint32_t end_ch = 0);

    /// Pointer to the internal server info array (MAX_SERVER_CONNECTIONS entries, 0-based).
    const ServerInfo* server_info_array() const;

    /// Active configuration.
    const NetworkLteConfig& config() const;

    /// Replace the active configuration (takes effect on the next step cycle).
    void set_config(const NetworkLteConfig& config);