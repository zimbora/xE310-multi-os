#include "modem/i_radio_lte.h"
#include "modem/network_lte.h"

namespace modem {

void process_radio_requests(RadioLteChannels& channels, IRadioLte& radio) {
    ModemTxMsg req{};
    while (channels.recv_request(req) == MessageChannelError::ok) {
        switch (req.type) {
            case RadioLteRequestType::get_registration_info:
                channels.publish_registration_info(radio.registration_info());
                break;
            case RadioLteRequestType::scan_networks:
                channels.send_response(radio.scan_networks(req.arg0, req.arg1));
                break;
            case RadioLteRequestType::get_signal_quality:
            case RadioLteRequestType::get_iccid:
            case RadioLteRequestType::get_imsi:
            case RadioLteRequestType::get_modem_info:
            case RadioLteRequestType::get_sim_status:
            case RadioLteRequestType::get_radio_tech:
            case RadioLteRequestType::get_reg_status:
            case RadioLteRequestType::get_network_info:
            case RadioLteRequestType::get_psm_mode:
            case RadioLteRequestType::get_cpsms_config:
            case RadioLteRequestType::get_telit_cpsms_config:
            case RadioLteRequestType::get_telit_cpsms_status:
            case RadioLteRequestType::get_network_survey_result:
            case RadioLteRequestType::get_available_operators:
            case RadioLteRequestType::get_csurv_result:
            case RadioLteRequestType::get_server_info_array:
            case RadioLteRequestType::get_config:
            case RadioLteRequestType::set_config: channels.send_response(true); break;
            default: channels.send_response(false); break;
        }
    }
}

} // namespace modem
