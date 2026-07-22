#pragma once

#include "hal/fixed_string.h"
#include "modem/xe310.h"

#include <cstdint>

// Choose one MVNO
// #define IDEMIA_PUBLIC
#define TELENOR_PUBLIC
// #define ONET_PUBLIC
//  --- --- ---
#define COUNTRY_CODE 268
#define DEFAULT_IOT_TECH RadioTech::cat_m1
#define FALLBACK_IOT_TECH RadioTech::cat_m1
#define DEFAULT_PLMN "26801" // VDF
// #define DEFAULT_PLMN "26803" // NOS
// #define DEFAULT_PLMN "26806" // MEO

#ifdef ONET_PUBLIC
#define DEFAULT_APN "terminal.apn"
#define FALLBACK_APN "terminal.apn"
#elif defined(TELENOR_PUBLIC)
#define DEFAULT_APN "connect.cxn"
#define FALLBACK_APN "connect.cxn"
#elif defined(IDEMIA_PUBLIC)
#define DEFAULT_APN "lpwa.vodafone.iot"
#define FALLBACK_APN "lpwa.vodafone.iot"
#else
#define DEFAULT_APN "anova.apn"
#define FALLBACK_APN "anova.apn"
#endif

#define B8_20_BAND_MASK 524416
#define B3_8_20_BAND_MASK 524420
#define B1_3_8_20_28_BAND_MASK 134742149

// PT
// MEO CATM1 BANDS: 20
// Vodafone CATM1 BANDS: 8, 20
// NOS CATM1 BANDS: 3, 20

#if defined(COUNTRY_CODE) && COUNTRY_CODE == 268
#define DEFAULT_LTE_BANDS B3_8_20_BAND_MASK
#define FALLBACK_LTE_BANDS B3_8_20_BAND_MASK
#else
#define DEFAULT_LTE_BANDS B3_8_20_BAND_MASK
#define FALLBACK_LTE_BANDS B1_3_8_20_28_BAND_MASK
#endif

namespace modem {

/// Configuration for the LTE network state machine.
struct NetworkLteConfig {
    uint8_t cid = 1;                     ///< PDP context ID to use for LTE data connection (default 1, must be >0)
    uint8_t attach_timeout_sec = 120;    ///< Timeout for network attach in seconds
    uint8_t pdp_timeout_sec = 15;        ///< Timeout for PDP context
    uint8_t data_ready_timeout_sec = 30; ///< Timeout for server connection in seconds
    uint16_t transparent_timeout_sec =
        300; ///< Timeout for transparent mode in seconds, adjust as needed based on expected time to send AT commands
             ///< and receive responses in transparent mode
    uint8_t max_network_attempts = 2; ///< Timeout for server connection and data transfer
    uint8_t max_attach_retries = 2;
    uint8_t max_pdp_retries = 2;

    uint64_t default_lte_bands = DEFAULT_LTE_BANDS;
    RadioTech default_iot_tech = DEFAULT_IOT_TECH;
    FixedString<MODEM_MEDIUM_STR> default_apn{DEFAULT_APN}; // 1oT

    uint64_t fallback_lte_bands = FALLBACK_LTE_BANDS;
    RadioTech fallback_iot_tech = FALLBACK_IOT_TECH;
    FixedString<MODEM_MEDIUM_STR> fallback_apn{FALLBACK_APN}; // telenor private; connect.cxn telenor public

    FixedString<MODEM_SHORT_STR> plmn{
        DEFAULT_PLMN}; ///< Optional PLMN to attach to (e.g. "26801" for VDF PT). If empty, modem default will be used.

    bool fPsmEnable = true;    ///< Whether to use PSM if available on the network
    bool fCfunSleep = true;    ///< Whether to use CFUN=4 + CFUN=11 to enter sleep mode (if supported by modem)
    uint32_t psm_t3412 = 3600; ///< Sleep time in PSM mode, in seconds
    uint32_t psm_t3324 = 60;   ///< Active time in PSM mode, in seconds

    uint8_t conn_id = 1; ///< Connection ID to query for server connection status (e.g. for UDP sockets)
};

} // namespace modem
