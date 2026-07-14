#include "i_radio_lte_internal.h"
#include "modem/network_lte.h"

namespace modem {

void process_radio_requests(RadioLteChannels& channels, NetworkLte& radio) {
    MessageFrame frame{};
    while (channels.recv_request_frame(frame) == MessageChannelError::ok) {
        if (frame.length < sizeof(RadioLteRequestType)) {
            channels.publish_typed_response(false);
            continue;
        }

        RadioLteRequestType request_type = RadioLteRequestType::get_registration_info;
        std::memcpy(&request_type, frame.data.data(), sizeof(request_type));

        ModemTxMsg req{};
        if (request_type != RadioLteRequestType::set_config && request_type != RadioLteRequestType::server_connect) {
            if (frame.length != sizeof(ModemTxMsg)) {
                channels.publish_typed_response(false);
                continue;
            }
            std::memcpy(&req, frame.data.data(), sizeof(req));
            request_type = req.type;
        }

        switch (request_type) {
            case RadioLteRequestType::get_registration_info:
                channels.publish_typed_response(radio.registration_info());
                break;

            case RadioLteRequestType::get_signal_quality:
                channels.publish_typed_response(radio.signal_quality());
                break;

            case RadioLteRequestType::get_iccid: channels.publish_typed_response(radio.iccid()); break;

            case RadioLteRequestType::get_imsi: channels.publish_typed_response(radio.imsi()); break;

            case RadioLteRequestType::get_clock: channels.publish_typed_response(radio.clock()); break;

            case RadioLteRequestType::get_modem_info: channels.publish_typed_response(radio.modem_info()); break;

            case RadioLteRequestType::get_sim_status: channels.publish_typed_response(radio.sim_status()); break;

            case RadioLteRequestType::get_radio_tech: channels.publish_typed_response(radio.radio_tech()); break;

            case RadioLteRequestType::get_reg_status: channels.publish_typed_response(radio.reg_status()); break;

            case RadioLteRequestType::get_network_info: channels.publish_typed_response(radio.network_info()); break;

            case RadioLteRequestType::get_psm_mode: channels.publish_typed_response(radio.psm_mode()); break;

            case RadioLteRequestType::get_cpsms_config: channels.publish_typed_response(radio.cpsms_config()); break;

            case RadioLteRequestType::get_telit_cpsms_config:
                channels.publish_typed_response(radio.telit_cpsms_config());
                break;

            case RadioLteRequestType::get_telit_cpsms_status:
                channels.publish_typed_response(radio.telit_cpsms_status());
                break;

            case RadioLteRequestType::get_network_survey_result:
                channels.publish_typed_response(radio.network_survey_result());
                break;

            case RadioLteRequestType::get_available_operators:
                channels.publish_typed_response(radio.available_operators());
                break;

            case RadioLteRequestType::get_csurv_result: channels.publish_typed_response(radio.csurv_result()); break;

            case RadioLteRequestType::scan_networks:
                channels.publish_typed_response(radio.scan_networks(req.arg0, req.arg1));
                break;

            case RadioLteRequestType::get_server_info_array:
                channels.publish_typed_response(radio.server_info_array());
                break;

            case RadioLteRequestType::get_config: channels.publish_typed_response(radio.config()); break;

            case RadioLteRequestType::set_config: {
                if (frame.length != sizeof(ModemSetConfigMsg)) {
                    channels.publish_typed_response(false);
                    break;
                }

                ModemSetConfigMsg set_cfg_msg{};
                std::memcpy(&set_cfg_msg, frame.data.data(), sizeof(set_cfg_msg));

                radio.set_config(set_cfg_msg.config);
                channels.publish_typed_response(true);
                break;
            }

            case RadioLteRequestType::network_connect: channels.publish_typed_response(radio.network_connect()); break;

            case RadioLteRequestType::network_disconnect:
                channels.publish_typed_response(radio.network_disconnect());
                break;

            case RadioLteRequestType::server_connect: {
                if (frame.length != sizeof(ModemServerConnectMsg)) {
                    channels.publish_typed_response(false);
                    break;
                }
                ModemServerConnectMsg sc_msg{};
                std::memcpy(&sc_msg, frame.data.data(), sizeof(sc_msg));
                channels.publish_typed_response(
                    radio.server_connect(sc_msg.conn_id, sc_msg.protocol.view(), sc_msg.ip.view(), sc_msg.port));
                break;
            }

            case RadioLteRequestType::server_disconnect:
                channels.publish_typed_response(radio.server_disconnect(static_cast<uint8_t>(req.arg0)));
                break;

            case RadioLteRequestType::force_psm: channels.publish_typed_response(radio.force_psm()); break;

            default: channels.publish_typed_response(false); break;
        }
    }
}

} // namespace modem
