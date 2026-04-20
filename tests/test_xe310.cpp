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
                std::string resp = response_str + "\r\nOK\r\n";
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
    EXPECT_EQ(modem_->request_imei_sv(imei_sv), ModemStatus::ok);
    EXPECT_EQ(imei_sv, "1234567890123456");
}

TEST_F(Xe310Test, RequestModelId) {
    expect_command_ok("AT#CGMM", "ME310G1-W1");
    std::string model;
    EXPECT_EQ(modem_->request_model_id(model), ModemStatus::ok);
    EXPECT_EQ(model, "ME310G1-W1");
}

TEST_F(Xe310Test, RequestTelitId) {
    expect_command_ok("AT#TID", "12345");
    std::string tid;
    EXPECT_EQ(modem_->request_telit_id(tid), ModemStatus::ok);
    EXPECT_EQ(tid, "12345");
}

TEST_F(Xe310Test, RequestIdentification) {
    expect_command_ok("ATI", "Telit ME310G1-W1");
    std::string info;
    EXPECT_EQ(modem_->request_identification(info), ModemStatus::ok);
    EXPECT_EQ(info, "Telit ME310G1-W1");
}

TEST_F(Xe310Test, RequestImeiSvError) {
    expect_command_error("AT+IMEISV");
    std::string imei_sv;
    EXPECT_EQ(modem_->request_imei_sv(imei_sv), ModemStatus::at_error);
    EXPECT_TRUE(imei_sv.empty());
}

// --- SIM Card ---

TEST_F(Xe310Test, ReadIccid) {
    expect_command_ok("AT+CCID", "8935101234567890123");
    std::string iccid;
    EXPECT_EQ(modem_->read_iccid(iccid), ModemStatus::ok);
    EXPECT_EQ(iccid, "8935101234567890123");
}

TEST_F(Xe310Test, ReadImsi) {
    expect_command_ok("AT+CIMI", "214011234567890");
    std::string imsi;
    EXPECT_EQ(modem_->read_imsi(imsi), ModemStatus::ok);
    EXPECT_EQ(imsi, "214011234567890");
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
    expect_command_ok("AT#QSS?", "1");
    SimStatus status = SimStatus::not_inserted;
    EXPECT_EQ(modem_->query_sim_status(status), ModemStatus::ok);
    EXPECT_EQ(status, SimStatus::inserted);
}

TEST_F(Xe310Test, QuerySimStatusReady) {
    expect_command_ok("AT#QSS?", "3");
    SimStatus status = SimStatus::not_inserted;
    EXPECT_EQ(modem_->query_sim_status(status), ModemStatus::ok);
    EXPECT_EQ(status, SimStatus::inserted_and_ready);
}

TEST_F(Xe310Test, SendSimCommand) {
    expect_command_ok("AT+CSIM=10,\"A0A40000\"", "+CSIM: 4,\"9000\"");
    std::string sim_response;
    EXPECT_EQ(modem_->send_sim_command("A0A40000", sim_response), ModemStatus::ok);
}

// --- Network Registration ---

TEST_F(Xe310Test, SetBands) {
    expect_command_ok("AT#BND=0,0,524420,0,0", "");
    EXPECT_EQ(modem_->set_bands(0, 0, 524420, 0, 0), ModemStatus::ok);
}

TEST_F(Xe310Test, GetBands) {
    expect_command_ok("AT#BND?", "#BND: 0,0,524420,0,0");
    std::string bands;
    EXPECT_EQ(modem_->get_bands(bands), ModemStatus::ok);
    EXPECT_EQ(bands, "#BND: 0,0,524420,0,0");
}

TEST_F(Xe310Test, GetRegistrationStatusHome) {
    expect_command_ok("AT+CEREG?", "1");
    RegStatus status = RegStatus::not_registered;
    EXPECT_EQ(modem_->get_registration_status(status), ModemStatus::ok);
    EXPECT_EQ(status, RegStatus::registered_home);
}

TEST_F(Xe310Test, GetRegistrationStatusSearching) {
    expect_command_ok("AT+CEREG?", "2");
    RegStatus status = RegStatus::not_registered;
    EXPECT_EQ(modem_->get_registration_status(status), ModemStatus::ok);
    EXPECT_EQ(status, RegStatus::searching);
}

TEST_F(Xe310Test, GetSignalQuality) {
    expect_command_ok("AT+CESQ", "20,3,255,255,40,50");
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
    EXPECT_EQ(modem_->get_operator(oper), ModemStatus::ok);
    EXPECT_EQ(oper, "+COPS: 0,2,\"21401\",7");
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
    // First call: AT#SSENDEXT command
    EXPECT_CALL(*mock_uart_, write(_, _))
        .WillOnce(Invoke([](const uint8_t* data, size_t length) {
            std::string sent(reinterpret_cast<const char*>(data), length);
            EXPECT_EQ(sent, "AT#SSENDEXT=1,5\r\n");
            return UartError::ok;
        }))
        // Second call: binary payload
        .WillOnce(Invoke([](const uint8_t* data, size_t length) {
            EXPECT_EQ(length, 5u);
            EXPECT_EQ(data[0], 0x48); // 'H'
            return UartError::ok;
        }));

    // First read: prompt '>'
    EXPECT_CALL(*mock_uart_, read(_, _, _, _))
        .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
            std::string resp = "\r\n> ";
            std::memcpy(buffer, resp.c_str(), resp.size());
            bytes_read = resp.size();
            return UartError::ok;
        }))
        // Second read: OK after payload
        .WillOnce(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
            std::string resp = "\r\nOK\r\n";
            std::memcpy(buffer, resp.c_str(), resp.size());
            bytes_read = resp.size();
            return UartError::ok;
        }));

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    EXPECT_EQ(modem_->udp_send(1, payload), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpReceive) {
    expect_command_ok("AT#SRECV=1,1500", "#SRECV: 1,5\nHello");
    std::vector<uint8_t> data;
    EXPECT_EQ(modem_->udp_receive(1, data), ModemStatus::ok);
    std::vector<uint8_t> expected = {'H', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(data, expected);
}

TEST_F(Xe310Test, UdpReceiveCustomMaxBytes) {
    expect_command_ok("AT#SRECV=1,256", "#SRECV: 1,3\nABC");
    std::vector<uint8_t> data;
    EXPECT_EQ(modem_->udp_receive(1, data, 256), ModemStatus::ok);
    std::vector<uint8_t> expected = {'A', 'B', 'C'};
    EXPECT_EQ(data, expected);
}

TEST_F(Xe310Test, UdpClose) {
    expect_command_ok("AT#SH=1", "");
    EXPECT_EQ(modem_->udp_close(1), ModemStatus::ok);
}

TEST_F(Xe310Test, UdpStatus) {
    expect_command_ok("AT#SS=1", "#SS: 1,2");
    uint8_t state = 0;
    EXPECT_EQ(modem_->udp_status(1, state), ModemStatus::ok);
    EXPECT_EQ(state, 2);
}

TEST_F(Xe310Test, UdpOpenError) {
    expect_command_error("AT#SD=1,1,5000,\"192.168.1.100\",0,0,1,1");
    EXPECT_EQ(modem_->udp_open(1, "192.168.1.100", 5000), ModemStatus::at_error);
}

TEST_F(Xe310Test, UdpCloseTimeout) {
    expect_command_timeout();
    EXPECT_EQ(modem_->udp_close(1), ModemStatus::timeout);
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