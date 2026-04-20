#include "modem/xe310.h"
#include "modem/modem_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace modem;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::Invoke;

class MockUart : public UartInterface {
public:
    MOCK_METHOD(UartError, open, (const char* port, const UartConfig& config), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, is_open, (), (const, override));
    MOCK_METHOD(UartError, write, (const uint8_t* data, size_t length), (override));
    MOCK_METHOD(UartError, read,
                (uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms),
                (override));
};

class Xe310Test : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock = std::make_unique<MockUart>();
        mock_uart_ = mock.get();
        controller_ = std::make_unique<ModemController>(std::move(mock));
        modem_ = std::make_unique<xE310>(*controller_);

        ON_CALL(*mock_uart_, is_open()).WillByDefault(Return(true));
    }

    /// Simulate a full AT exchange: echo + response body + OK
    void expect_command_ok(const std::string& expected_cmd, const std::string& response_str) {
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Invoke([expected_cmd](const uint8_t* data, size_t length) {
                std::string sent(reinterpret_cast<const char*>(data), length);
                EXPECT_EQ(sent, expected_cmd + "\r\n");
                return UartError::ok;
            }));

        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([response_str](uint8_t* buffer, size_t buffer_size,
                                             size_t& bytes_read, uint32_t) {
                std::string resp;
                if (response_str.empty()) {
                    resp = "\r\nOK\r\n";
                } else {
                    resp = "\r\n" + response_str + "\r\n\r\nOK\r\n";
                }
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    void expect_command_error(const std::string& expected_cmd) {
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));

        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = "\r\nERROR\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    void expect_command_timeout() {
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));

        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Return(UartError::timeout));
    }

    MockUart* mock_uart_ = nullptr;
    std::unique_ptr<ModemController> controller_;
    std::unique_ptr<xE310> modem_;
};

// --- Basic Commands ---

TEST_F(Xe310Test, AtOkSuccess) {
    expect_command_ok("AT", "");
    EXPECT_EQ(modem_->at_ok(), ModemStatus::ok);
}

TEST_F(Xe310Test, AtOkTimeout) {
    expect_command_timeout();
    EXPECT_EQ(modem_->at_ok(), ModemStatus::timeout);
}

TEST_F(Xe310Test, SetBaudrate) {
    expect_command_ok("AT+IPR=115200", "");
    EXPECT_EQ(modem_->set_baudrate(115200), ModemStatus::ok);
}

TEST_F(Xe310Test, SetEchoOn) {
    expect_command_ok("ATE1", "");
    EXPECT_EQ(modem_->set_echo(true), ModemStatus::ok);
}

TEST_F(Xe310Test, SetEchoOff) {
    expect_command_ok("ATE0", "");
    EXPECT_EQ(modem_->set_echo(false), ModemStatus::ok);
}

// --- Identification ---

TEST_F(Xe310Test, RequestImeiSv) {
    expect_command_ok("AT+IMEISV", "1234567890123456");
    std::string imei_sv;
    auto status = modem_->request_imei_sv(imei_sv);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(imei_sv.empty());
}

TEST_F(Xe310Test, RequestModelId) {
    expect_command_ok("AT#CGMM", "ME310G1-W1");
    std::string model;
    auto status = modem_->request_model_id(model);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(model.empty());
}

TEST_F(Xe310Test, RequestTelitId) {
    expect_command_ok("AT#TID", "12345");
    std::string tid;
    auto status = modem_->request_telit_id(tid);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(tid.empty());
}

TEST_F(Xe310Test, RequestIdentification) {
    expect_command_ok("ATI", "Telit ME310G1-W1");
    std::string info;
    auto status = modem_->request_identification(info);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(info.empty());
}

TEST_F(Xe310Test, RequestImeiSvError) {
    expect_command_error("AT+IMEISV");
    std::string imei_sv;
    EXPECT_EQ(modem_->request_imei_sv(imei_sv), ModemStatus::at_error);
    EXPECT_TRUE(imei_sv.empty());
}

// --- SIM Card ---

TEST_F(Xe310Test, ReadIccid) {
    expect_command_ok("AT+CCID", "+CCID: 8935101234567890123");
    std::string iccid;
    auto status = modem_->read_iccid(iccid);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(iccid.empty());
}

TEST_F(Xe310Test, ReadImsi) {
    expect_command_ok("AT+CIMI", "214011234567890");
    std::string imsi;
    auto status = modem_->read_imsi(imsi);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(imsi.empty());
}

TEST_F(Xe310Test, SetSimDetectionGpio) {
    expect_command_ok("AT#SIMDET=0", "");
    EXPECT_EQ(modem_->set_sim_detection(SimDetMode::gpio), ModemStatus::ok);
}

TEST_F(Xe310Test, SetSimDetectionAlways) {
    expect_command_ok("AT#SIMDET=1", "");
    EXPECT_EQ(modem_->set_sim_detection(SimDetMode::always), ModemStatus::ok);
}

TEST_F(Xe310Test, QuerySimStatusInserted) {
    expect_command_ok("AT#QSS?", "#QSS: 0,1");
    SimStatus status = SimStatus::not_inserted;
    EXPECT_EQ(modem_->query_sim_status(status), ModemStatus::ok);
    EXPECT_EQ(status, SimStatus::inserted);
}

TEST_F(Xe310Test, QuerySimStatusReady) {
    expect_command_ok("AT#QSS?", "#QSS: 0,3");
    SimStatus status = SimStatus::not_inserted;
    EXPECT_EQ(modem_->query_sim_status(status), ModemStatus::ok);
    EXPECT_EQ(status, SimStatus::inserted_and_ready);
}

TEST_F(Xe310Test, SendSimCommand) {
    expect_command_ok("AT+CSIM=8,\"A0A40000\"", "+CSIM: 4,\"9000\"");
    std::string sim_response;
    auto status = modem_->send_sim_command("A0A40000", sim_response);
    EXPECT_EQ(status, ModemStatus::ok);
}

// --- Network Registration ---

TEST_F(Xe310Test, SetBands) {
    expect_command_ok("AT#BND=0,0,524420,0,0", "");
    EXPECT_EQ(modem_->set_bands(0, 0, 524420, 0, 0), ModemStatus::ok);
}

TEST_F(Xe310Test, GetBands) {
    expect_command_ok("AT#BND?", "#BND: 0,0,524420,0,0");
    std::string bands;
    auto status = modem_->get_bands(bands);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(bands.empty());
}

TEST_F(Xe310Test, GetRegistrationStatusHome) {
    expect_command_ok("AT+CEREG?", "+CEREG: 0,1");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 0);
    EXPECT_EQ(info.stat, RegStatus::registered_home);
    EXPECT_FALSE(info.has_location);
}

TEST_F(Xe310Test, GetRegistrationStatusSearching) {
    expect_command_ok("AT+CEREG?", "+CEREG: 0,2");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 0);
    EXPECT_EQ(info.stat, RegStatus::searching);
    EXPECT_FALSE(info.has_location);
}

TEST_F(Xe310Test, GetRegistrationStatusRoaming) {
    expect_command_ok("AT+CEREG?", "+CEREG: 2,5,\"0001\",\"0000A1B2\",7");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 2);
    EXPECT_EQ(info.stat, RegStatus::registered_roaming);
    EXPECT_TRUE(info.has_location);
    EXPECT_EQ(info.lac, "0001");
    EXPECT_EQ(info.ci, "0000A1B2");
    EXPECT_EQ(info.act, RadioTech::lte);
}

TEST_F(Xe310Test, GetRegistrationStatusDenied) {
    expect_command_ok("AT+CEREG?", "+CEREG: 0,3");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 0);
    EXPECT_EQ(info.stat, RegStatus::denied);
    EXPECT_FALSE(info.has_location);
}

TEST_F(Xe310Test, GetRegistrationStatusWithLocationCatM1) {
    expect_command_ok("AT+CEREG?", "+CEREG: 2,1,\"00FF\",\"01234ABC\",8");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 2);
    EXPECT_EQ(info.stat, RegStatus::registered_home);
    EXPECT_TRUE(info.has_location);
    EXPECT_EQ(info.lac, "00FF");
    EXPECT_EQ(info.ci, "01234ABC");
    EXPECT_EQ(info.act, RadioTech::cat_m1);
}

TEST_F(Xe310Test, GetRegistrationStatusWithLocationNbIot) {
    expect_command_ok("AT+CEREG?", "+CEREG: 2,5,\"1A2B\",\"DEADBEEF\",9");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 2);
    EXPECT_EQ(info.stat, RegStatus::registered_roaming);
    EXPECT_TRUE(info.has_location);
    EXPECT_EQ(info.lac, "1A2B");
    EXPECT_EQ(info.ci, "DEADBEEF");
    EXPECT_EQ(info.act, RadioTech::nb_iot);
}

TEST_F(Xe310Test, GetRegistrationStatusNotRegistered) {
    expect_command_ok("AT+CEREG?", "+CEREG: 0,0");
    RegistrationInfo info;
    EXPECT_EQ(modem_->get_registration_status(info), ModemStatus::ok);
    EXPECT_EQ(info.mode, 0);
    EXPECT_EQ(info.stat, RegStatus::not_registered);
    EXPECT_FALSE(info.has_location);
}

TEST_F(Xe310Test, GetSignalQuality) {
    expect_command_ok("AT+CESQ", "+CESQ: 20,3,255,255,40,50");
    SignalQuality sq;
    EXPECT_EQ(modem_->get_signal_quality(sq), ModemStatus::ok);
    EXPECT_EQ(sq.rssi, 20);
    EXPECT_EQ(sq.ber, 3);
    EXPECT_EQ(sq.rsrq, 40);
    EXPECT_EQ(sq.rsrp, 50);
}

TEST_F(Xe310Test, SetRadioTechLte) {
    expect_command_ok("AT+COPS=0,,,7", "");
    EXPECT_EQ(modem_->set_radio_tech(RadioTech::lte), ModemStatus::ok);
}

TEST_F(Xe310Test, SetRadioTechCatM1) {
    expect_command_ok("AT+COPS=0,,,8", "");
    EXPECT_EQ(modem_->set_radio_tech(RadioTech::cat_m1), ModemStatus::ok);
}

TEST_F(Xe310Test, SetRadioTechNbIot) {
    expect_command_ok("AT+COPS=0,,,9", "");
    EXPECT_EQ(modem_->set_radio_tech(RadioTech::nb_iot), ModemStatus::ok);
}

TEST_F(Xe310Test, SetOperatorManual) {
    expect_command_ok("AT+COPS=4,2,\"21401\",7", "");
    EXPECT_EQ(modem_->set_operator_manual("21401", RadioTech::lte), ModemStatus::ok);
}

TEST_F(Xe310Test, SetOperatorAuto) {
    expect_command_ok("AT+COPS=0", "");
    EXPECT_EQ(modem_->set_operator_auto(), ModemStatus::ok);
}

TEST_F(Xe310Test, GetOperator) {
    expect_command_ok("AT+COPS?", "+COPS: 0,2,\"21401\",7");
    std::string oper;
    auto status = modem_->get_operator(oper);
    EXPECT_EQ(status, ModemStatus::ok);
    EXPECT_FALSE(oper.empty());
}

// --- Network Attach ---

TEST_F(Xe310Test, SetApn) {
    expect_command_ok("AT+CGDCONT=1,\"IP\",\"internet\"", "");
    EXPECT_EQ(modem_->set_apn(1, "internet"), ModemStatus::ok);
}

TEST_F(Xe310Test, GetApn) {
    expect_command_ok("AT+CGDCONT?", "+CGDCONT: 1,\"IP\",\"internet\"");
    std::string apn;
    EXPECT_EQ(modem_->get_apn(1, apn), ModemStatus::ok);
}

TEST_F(Xe310Test, ActivatePdp) {
    expect_command_ok("AT+CGACT=1,1", "");
    EXPECT_EQ(modem_->activate_pdp(1), ModemStatus::ok);
}

TEST_F(Xe310Test, DeactivatePdp) {
    expect_command_ok("AT+CGACT=0,1", "");
    EXPECT_EQ(modem_->deactivate_pdp(1), ModemStatus::ok);
}

TEST_F(Xe310Test, GetPdpStateActive) {
    expect_command_ok("AT+CGACT?", "+CGACT: 1,1");
    bool active = false;
    EXPECT_EQ(modem_->get_pdp_state(1, active), ModemStatus::ok);
    EXPECT_TRUE(active);
}

TEST_F(Xe310Test, GetPdpStateInactive) {
    expect_command_ok("AT+CGACT?", "+CGACT: 1,0");
    bool active = true;
    EXPECT_EQ(modem_->get_pdp_state(1, active), ModemStatus::ok);
    EXPECT_FALSE(active);
}

TEST_F(Xe310Test, GetIpAddress) {
    expect_command_ok("AT+CGPADDR=1", "+CGPADDR: 1,\"10.0.0.1\"");
    std::string ip;
    EXPECT_EQ(modem_->get_ip_address(1, ip), ModemStatus::ok);
    EXPECT_EQ(ip, "10.0.0.1");
}

TEST_F(Xe310Test, GetPdpInfo) {
    expect_command_ok("AT+CGCONTRDP=1",
                      "+CGCONTRDP: 1,5,\"internet\",\"10.0.0.1\",\"10.0.0.254\",\"8.8.8.8\",\"8.8.4.4\"");
    std::string ip, gw, dns1, dns2;
    EXPECT_EQ(modem_->get_pdp_info(1, ip, gw, dns1, dns2), ModemStatus::ok);
    EXPECT_EQ(ip, "10.0.0.1");
    EXPECT_EQ(gw, "10.0.0.254");
    EXPECT_EQ(dns1, "8.8.8.8");
    EXPECT_EQ(dns2, "8.8.4.4");
}

TEST_F(Xe310Test, ActivatePdpError) {
    expect_command_error("AT+CGACT=1,1");
    EXPECT_EQ(modem_->activate_pdp(1), ModemStatus::at_error);
}

// --- UDP Connection ---

TEST_F(Xe310Test, UdpOpen) {
    expect_command_ok("AT#SD=1,1,5000,\"192.168.1.100\",0,4000,1,1", "");
    EXPECT_EQ(modem_->udp_open(1, "192.168.1.100", 5000, 4000, 1), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpOpenDefaultParams) {
    expect_command_ok("AT#SD=1,1,5000,\"192.168.1.100\",0,0,1,1", "");
    EXPECT_EQ(modem_->udp_open(1, "192.168.1.100", 5000), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpListen) {
    expect_command_ok("AT#SL=1,1,4000,255,1", "");
    EXPECT_EQ(modem_->udp_listen(1, 4000, 1), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpSend) {
    {
        testing::InSequence seq;

        // Step 1: AT#SSENDEXT command is written to UART
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Invoke([](const uint8_t* data, size_t length) {
                std::string sent(reinterpret_cast<const char*>(data), length);
                EXPECT_EQ(sent, "AT#SSENDEXT=1,5\r\n");
                return UartError::ok;
            }));

        // Step 2: Modem responds with "\r\n> " prompt
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                // IRA: 13, 10, 62, 32
                uint8_t prompt[] = {'\r', '\n', '>', ' '};
                std::memcpy(buffer, prompt, sizeof(prompt));
                bytes_read = sizeof(prompt);
                return UartError::ok;
            }));

        // Step 3: Binary payload is sent (all 5 bytes at once)
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Invoke([](const uint8_t* data, size_t length) {
                EXPECT_EQ(length, 5u);
                EXPECT_EQ(data[0], 0x48); // 'H'
                EXPECT_EQ(data[1], 0x65); // 'e'
                EXPECT_EQ(data[2], 0x6C); // 'l'
                EXPECT_EQ(data[3], 0x6C); // 'l'
                EXPECT_EQ(data[4], 0x6F); // 'o'
                return UartError::ok;
            }));

        // Step 4: Modem responds with OK after all bytes received
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = "\r\nOK\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    EXPECT_EQ(modem_->udp_send(1, payload), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpSendPromptTimeout) {
    {
        testing::InSequence seq;

        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));

        // Modem never sends the prompt
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Return(UartError::timeout));
    }

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    EXPECT_EQ(modem_->udp_send(1, payload), ModemStatus::timeout);
}

TEST_F(Xe310Test, UdpSendNoPrompt) {
    {
        testing::InSequence seq;

        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));

        // Modem responds with ERROR instead of prompt
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = "\r\nERROR\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    EXPECT_EQ(modem_->udp_send(1, payload), ModemStatus::at_error);
}

TEST_F(Xe310Test, UdpSendWritePayloadFails) {
    {
        testing::InSequence seq;

        // Command sent OK
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));

        // Prompt received
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                uint8_t prompt[] = {'\r', '\n', '>', ' '};
                std::memcpy(buffer, prompt, sizeof(prompt));
                bytes_read = sizeof(prompt);
                return UartError::ok;
            }));

        // Payload write fails
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::write_failed));
    }

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    EXPECT_EQ(modem_->udp_send(1, payload), ModemStatus::uart_error);
}

TEST_F(Xe310Test, UdpSendBinaryData) {
    {
        testing::InSequence seq;

        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Invoke([](const uint8_t* data, size_t length) {
                std::string sent(reinterpret_cast<const char*>(data), length);
                EXPECT_EQ(sent, "AT#SSENDEXT=2,4\r\n");
                return UartError::ok;
            }));

        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                uint8_t prompt[] = {'\r', '\n', '>', ' '};
                std::memcpy(buffer, prompt, sizeof(prompt));
                bytes_read = sizeof(prompt);
                return UartError::ok;
            }));

        // All octets 0x00-0xFF are valid with SSENDEXT
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Invoke([](const uint8_t* data, size_t length) {
                EXPECT_EQ(length, 4u);
                EXPECT_EQ(data[0], 0x00);
                EXPECT_EQ(data[1], 0x1A); // would be Ctrl+Z with SSEND
                EXPECT_EQ(data[2], 0xFF);
                EXPECT_EQ(data[3], 0x7E);
                return UartError::ok;
            }));

        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = "\r\nOK\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    // Test with special bytes that would break AT#SSEND but work with AT#SSENDEXT
    std::vector<uint8_t> payload = {0x00, 0x1A, 0xFF, 0x7E};
    EXPECT_EQ(modem_->udp_send(2, payload), ModemStatus::ok);
}

// --- Not Connected ---

TEST_F(Xe310Test, CommandWhenNotConnected) {
    EXPECT_CALL(*mock_uart_, is_open()).WillOnce(Return(false));
    EXPECT_EQ(modem_->at_ok(), ModemStatus::not_connected);
}

TEST_F(Xe310Test, UdpOpenWhenNotConnected) {
    EXPECT_CALL(*mock_uart_, is_open()).WillOnce(Return(false));
    EXPECT_EQ(modem_->udp_open(1, "192.168.1.100", 5000), ModemStatus::not_connected);
}