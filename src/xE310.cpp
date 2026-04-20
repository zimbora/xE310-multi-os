/*
 * xE310.cpp
 *
 * Cross-platform implementation for the Telit xE310 LTE-M / NB-IoT module.
 *
 * AT command reference: Telit xE310 AT Commands Reference Guide
 */

#include "xE310.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// -- Platform-specific GPIO ---------------------------------------------------

#ifdef XE310_PLATFORM_ARDUINO
  #define XE310_PIN_OUTPUT(pin)  pinMode((pin), OUTPUT)
  #define XE310_PIN_HIGH(pin)    digitalWrite((pin), HIGH)
  #define XE310_PIN_LOW(pin)     digitalWrite((pin), LOW)
#else
  #define XE310_PIN_OUTPUT(pin)  (void)(pin)
  #define XE310_PIN_HIGH(pin)    (void)(pin)
  #define XE310_PIN_LOW(pin)     (void)(pin)
#endif

// -- Internal String helpers --------------------------------------------------
// Provide a thin compatibility layer so that a single implementation file
// works whether String is Arduino::String or std::string.

#ifdef XE310_PLATFORM_ARDUINO

static bool _contains(const String& h, const char* n) {
    return h.indexOf(n) >= 0;
}
static String _trim(String s) {
    s.trim();
    return s;
}
static String _afterPrefix(const String& line, const char* prefix) {
    int idx = line.indexOf(prefix);
    if (idx < 0) return String("");
    return line.substring(idx + (int)strlen(prefix));
}
static String _makeStr(const char* buf) { return String(buf); }

static size_t _findStr(const String& s, const char* needle) {
    int idx = s.indexOf(needle);
    return (idx < 0) ? (size_t)(-1) : (size_t)idx;
}

static String _substr(const String& s, size_t from, size_t len = (size_t)(-1)) {
    if (len == (size_t)(-1)) return s.substring((unsigned int)from);
    return s.substring((unsigned int)from, (unsigned int)(from + len));
}

#else

#include <string>
#include <algorithm>
#include <cctype>

static bool _contains(const std::string& h, const char* n) {
    return h.find(n) != std::string::npos;
}
static std::string _trim(std::string s) {
    auto l = s.begin();
    while (l != s.end() && std::isspace((unsigned char)*l)) ++l;
    auto r = s.end();
    while (r != l && std::isspace((unsigned char)*std::prev(r))) --r;
    return std::string(l, r);
}
static std::string _afterPrefix(const std::string& line, const char* prefix) {
    auto pos = line.find(prefix);
    if (pos == std::string::npos) return std::string();
    return line.substr(pos + strlen(prefix));
}
static std::string _makeStr(const char* buf) { return std::string(buf); }

static size_t _findStr(const std::string& s, const char* needle) {
    return s.find(needle);
}

static std::string _substr(const std::string& s, size_t from, size_t len = std::string::npos) {
    return s.substr(from, len);
}

#endif

static const size_t NPOS = (size_t)(-1);

// -- Constructor --------------------------------------------------------------

XE310::XE310(ISerial* serial, int8_t pwkey)
    : _serial(serial), _pwkey(pwkey)
{
    memset(&_state, 0, sizeof(_state));
    _state.rssi      = 99;
    _state.regStatus = XE310_REG_NOT_REGISTERED;

    for (int i = 0; i < XE310_MAX_CONTEXTS; i++) {
        memset(&_ctx[i], 0, sizeof(_ctx[i]));
        _ctx[i].cid = (uint8_t)(i + 1);
    }
    for (int i = 0; i < XE310_MAX_SOCKETS; i++) {
        memset(&_sock[i], 0, sizeof(_sock[i]));
    }
}

// -- Port management ----------------------------------------------------------

bool XE310::begin(uint32_t baudrate) {
    if (_pwkey >= 0)
        XE310_PIN_OUTPUT(_pwkey);
    return _serial->begin(baudrate);
}

void XE310::end() {
    _serial->end();
}

// -- Initialisation -----------------------------------------------------------

bool XE310::init(uint8_t rat, uint16_t cops) {
    _state.rat  = rat;
    _state.cops = cops;

    if (!powerCycle())
        return false;
    if (!_config())
        return false;
    if (!_setRAT(rat, cops))
        return false;

    return true;
}

// -- Power management ---------------------------------------------------------

bool XE310::powerOn() {
    if (_pwkey < 0) return true;

    XE310_PIN_HIGH(_pwkey);
    XE310_DELAY(200);
    XE310_PIN_LOW(_pwkey);
    XE310_DELAY(5000);

    return _waitReady(15000);
}

bool XE310::powerOff() {
    if (_pwkey < 0) return true;

    sendCommand(_makeStr("AT#SHDN"), _makeStr("OK"), 5000);
    XE310_DELAY(3000);
    return true;
}

bool XE310::powerCycle() {
    if (_pwkey >= 0) {
        XE310_PIN_HIGH(_pwkey);
        XE310_DELAY(3000);
        XE310_PIN_LOW(_pwkey);
        XE310_DELAY(1000);
        XE310_PIN_HIGH(_pwkey);
        XE310_DELAY(200);
        XE310_PIN_LOW(_pwkey);
    }
    return _waitReady(20000);
}

bool XE310::softReset() {
    sendCommand(_makeStr("AT#REBOOT"), _makeStr("OK"), 5000);
    XE310_DELAY(5000);
    return _waitReady(30000);
}

// -- Module identification ----------------------------------------------------

String XE310::getIMEI(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+CGSN"), timeout));
}

String XE310::getICCID(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+ICCID"), _makeStr("+ICCID: "), timeout));
}

String XE310::getIMSI(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+CIMI"), timeout));
}

String XE310::getModel(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+CGMM"), timeout));
}

String XE310::getManufacturer(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+CGMI"), timeout));
}

String XE310::getFirmwareVersion(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+CGMR"), timeout));
}

// -- Network ------------------------------------------------------------------

bool XE310::isRegistered() {
    int8_t s = getRegistrationStatus();
    return (s == XE310_REG_REGISTERED_HOME || s == XE310_REG_ROAMING);
}

int8_t XE310::getRegistrationStatus() {
    _updateRegistration();
    return _state.regStatus;
}

int16_t XE310::getRSSI() {
    _updateRSSI();
    return _state.rssi;
}

String XE310::getOperator(uint32_t timeout) {
    return _trim(getCommand(_makeStr("AT+COPS?"), _makeStr("+COPS: "), timeout));
}

// -- PDP context --------------------------------------------------------------

bool XE310::setupAPN(uint8_t cid, String apn, String username, String password) {
    if (cid == 0 || cid > XE310_MAX_CONTEXTS)
        return false;

    const char* apn_c = apn.c_str();
    size_t alen = strlen(apn_c);
    if (alen >= sizeof(_ctx[cid-1].apn))
        alen = sizeof(_ctx[cid-1].apn) - 1;
    memset(_ctx[cid-1].apn, 0, sizeof(_ctx[cid-1].apn));
    memcpy(_ctx[cid-1].apn, apn_c, alen);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=%u,\"IP\",\"%s\"", cid, apn_c);
    if (!sendCommand(_makeStr(cmd), _makeStr("OK")))
        return false;

    if (username.length() > 0) {
        char auth[128];
        snprintf(auth, sizeof(auth), "AT+CGAUTH=%u,1,\"%s\",\"%s\"",
                 cid, username.c_str(), password.c_str());
        sendCommand(_makeStr(auth), _makeStr("OK"));
    }

    return true;
}

bool XE310::activatePDP(uint8_t cid) {
    if (cid == 0 || cid > XE310_MAX_CONTEXTS)
        return false;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SGACT=%u,1", cid);
    if (!sendCommand(_makeStr(cmd), _makeStr("OK"), 30000))
        return false;

    _ctx[cid-1].connected = true;
    return true;
}

bool XE310::deactivatePDP(uint8_t cid) {
    if (cid == 0 || cid > XE310_MAX_CONTEXTS)
        return false;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SGACT=%u,0", cid);
    sendCommand(_makeStr(cmd), _makeStr("OK"), 10000);
    _ctx[cid-1].connected = false;
    return true;
}

String XE310::getIP(uint8_t cid, uint32_t timeout) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CGPADDR=%u", cid);
    String resp = getCommand(_makeStr(cmd), _makeStr("+CGPADDR: "), timeout);

    // Response: <cid>,<addr>  -- skip cid field
    size_t comma = _findStr(resp, ",");
    if (comma != NPOS)
        return _trim(_substr(resp, comma + 1));
    return resp;
}

bool XE310::isContextActive(uint8_t cid) {
    if (cid == 0 || cid > XE310_MAX_CONTEXTS)
        return false;
    return _ctx[cid-1].connected;
}

// -- TCP ----------------------------------------------------------------------

bool XE310::tcpConnect(uint8_t socket, String host, uint16_t port,
                        uint8_t cid, uint32_t timeout) {
    if (socket >= XE310_MAX_SOCKETS)
        return false;

    char cfg[64];
    snprintf(cfg, sizeof(cfg), "AT#SCFG=%u,%u,300,90,600,50",
             socket + 1, cid);
    sendCommand(_makeStr(cfg), _makeStr("OK"));

    char dial[160];
    snprintf(dial, sizeof(dial), "AT#SD=%u,0,%u,\"%s\",0,0,0",
             socket + 1, port, host.c_str());

    if (!sendCommand(_makeStr(dial), _makeStr("CONNECT"), timeout)) {
        if (!sendCommand(_makeStr(dial), _makeStr("OK"), timeout))
            return false;
    }

    _sock[socket].connected   = true;
    _sock[socket].contextID   = cid;
    _sock[socket].dataPending = false;
    return true;
}

bool XE310::tcpClose(uint8_t socket) {
    if (socket >= XE310_MAX_SOCKETS)
        return false;

    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT#SH=%u", socket + 1);
    sendCommand(_makeStr(cmd), _makeStr("OK"), 5000);

    _sock[socket].connected   = false;
    _sock[socket].dataPending = false;
    return true;
}

bool XE310::tcpSend(uint8_t socket, const uint8_t* data, uint16_t len) {
    if (socket >= XE310_MAX_SOCKETS || !_sock[socket].connected)
        return false;
    if (data == nullptr || len == 0)
        return false;

    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT#SSEND=%u", socket + 1);
    _sendRaw(cmd);
    _sendRaw("\r\n");
    XE310_DELAY(50);

    String prompt = _readUntil(_makeStr(">"), 3000);
    if (!_contains(prompt, ">"))
        return false;

    _serial->write(data, len);
    uint8_t ctrlz = 0x1A;
    _serial->write(&ctrlz, 1);

    String resp = _readUntil(_makeStr("OK"), 5000);
    return _contains(resp, "OK");
}

bool XE310::tcpSend(uint8_t socket, const char* text) {
    if (text == nullptr) return false;
    return tcpSend(socket, (const uint8_t*)text, (uint16_t)strlen(text));
}

uint16_t XE310::tcpRecv(uint8_t socket, uint8_t* data, uint16_t maxLen,
                          uint32_t timeout) {
    if (socket >= XE310_MAX_SOCKETS || !_sock[socket].connected)
        return 0;
    if (data == nullptr || maxLen == 0)
        return 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#SRECV=%u,%u", socket + 1, maxLen);
    _sendRaw(cmd);
    _sendRaw("\r\n");

    String resp = _readUntil(_makeStr("OK"), timeout);
    if (resp.length() == 0)
        return 0;

    const char* hdr    = "#SRECV: ";
    size_t      hdrPos = _findStr(resp, hdr);
    if (hdrPos == NPOS)
        return 0;

    // Extract byte count: #SRECV: <n>,<len>
    String afterHdr = _substr(resp, hdrPos + strlen(hdr));
    size_t commaPos = _findStr(afterHdr, ",");
    if (commaPos == NPOS)
        return 0;

    String lenStr = _substr(afterHdr, commaPos + 1);
    size_t crPos  = _findStr(lenStr, "\r");
    if (crPos == NPOS)
        return 0;

    uint16_t recvLen = (uint16_t)strtoul(
                          _substr(lenStr, 0, crPos).c_str(), nullptr, 10);
    if (recvLen == 0)
        return 0;

    // Data follows the first newline after the header
    size_t dataStart = _findStr(resp, "\n");
    if (dataStart == NPOS || dataStart + 1 >= resp.length())
        return 0;
    dataStart++;

    uint16_t avail = (uint16_t)(resp.length() - dataStart);
    uint16_t copy  = (avail < maxLen) ? avail : maxLen;
    if (copy > recvLen) copy = recvLen;

    memcpy(data, resp.c_str() + dataStart, copy);
    return copy;
}

bool XE310::tcpIsConnected(uint8_t socket) {
    if (socket >= XE310_MAX_SOCKETS) return false;
    return _sock[socket].connected;
}

void XE310::tcpSetCloseCallback(void(*cb)(uint8_t socket)) {
    _tcpCloseCallback = cb;
}

// -- UDP ----------------------------------------------------------------------

bool XE310::udpOpen(uint8_t socket, uint16_t localPort, uint8_t cid) {
    if (socket >= XE310_MAX_SOCKETS)
        return false;

    char cfg[64];
    snprintf(cfg, sizeof(cfg), "AT#SCFG=%u,%u,300,90,600,50",
             socket + 1, cid);
    sendCommand(_makeStr(cfg), _makeStr("OK"));

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT#SBIND=%u,0,%u", socket + 1, localPort);
    if (!sendCommand(_makeStr(cmd), _makeStr("OK"), 10000))
        return false;

    _sock[socket].contextID = cid;
    _sock[socket].connected = true;
    return true;
}

bool XE310::udpClose(uint8_t socket) {
    return tcpClose(socket);
}

bool XE310::udpSend(uint8_t socket, String host, uint16_t port,
                     const uint8_t* data, uint16_t len) {
    if (socket >= XE310_MAX_SOCKETS)
        return false;

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "AT#SSENDUDP=%u,\"%s\",%u,%u",
             socket + 1, host.c_str(), port, len);
    _sendRaw(cmd);
    _sendRaw("\r\n");

    XE310_DELAY(50);
    String prompt = _readUntil(_makeStr(">"), 3000);
    if (!_contains(prompt, ">"))
        return false;

    _serial->write(data, len);
    uint8_t ctrlz = 0x1A;
    _serial->write(&ctrlz, 1);

    String resp = _readUntil(_makeStr("OK"), 5000);
    return _contains(resp, "OK");
}

// -- DNS ----------------------------------------------------------------------

String XE310::dnsResolve(String hostname, uint32_t timeout) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT#NSLOOKUP=\"%s\"", hostname.c_str());
    String resp = getCommand(_makeStr(cmd), _makeStr("#NSLOOKUP: "), timeout);

    size_t comma = _findStr(resp, ",");
    if (comma != NPOS)
        return _trim(_substr(resp, comma + 1));
    return _makeStr("");
}

// -- Ping ---------------------------------------------------------------------

int32_t XE310::ping(String host, uint8_t retries) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT#PING=\"%s\",%u,32,5000",
             host.c_str(), retries);
    String resp = getCommand(_makeStr(cmd), _makeStr("#PING: "), 15000);
    if (resp.length() == 0)
        return -1;

    // Find the last comma to get RTT field
    size_t lastComma = NPOS;
    size_t pos       = 0;
    while (true) {
        size_t next = _findStr(_substr(resp, pos), ",");
        if (next == NPOS) break;
        lastComma = pos + next;
        pos       = lastComma + 1;
    }
    if (lastComma == NPOS)
        return -1;

    return strtol(_substr(resp, lastComma + 1).c_str(), nullptr, 10);
}

// -- SMS ----------------------------------------------------------------------

bool XE310::sendSMS(String number, String message) {
    if (!sendCommand(_makeStr("AT+CMGF=1"), _makeStr("OK")))
        return false;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number.c_str());
    _sendRaw(cmd);
    _sendRaw("\r\n");

    XE310_DELAY(200);
    String prompt = _readUntil(_makeStr(">"), 5000);
    if (!_contains(prompt, ">"))
        return false;

    _sendRaw(message.c_str());
    uint8_t ctrlz = 0x1A;
    _serial->write(&ctrlz, 1);

    String resp = _readUntil(_makeStr("OK"), 15000);
    return _contains(resp, "+CMGS:") || _contains(resp, "OK");
}

bool XE310::setSMSHandler(void(*handler)(uint8_t, String, String)) {
    _smsHandler = handler;
    return sendCommand(_makeStr("AT+CNMI=2,1,0,0,0"), _makeStr("OK"));
}

// -- Clock --------------------------------------------------------------------

bool XE310::getNetworkTime(struct tm* t) {
    if (t == nullptr)
        return false;

    String resp = getCommand(_makeStr("AT+CCLK?"), _makeStr("+CCLK: "));
    if (resp.length() == 0)
        return false;

    // Strip surrounding quotes
    size_t q1 = _findStr(resp, "\"");
    if (q1 != NPOS) resp = _substr(resp, q1 + 1);
    size_t q2 = _findStr(resp, "\"");
    if (q2 != NPOS) resp = _substr(resp, 0, q2);

    memset(t, 0, sizeof(struct tm));
    int year = 0, mon = 0, day = 0, hr = 0, min = 0, sec = 0;
    sscanf(resp.c_str(), "%d/%d/%d,%d:%d:%d", &year, &mon, &day, &hr, &min, &sec);
    t->tm_year = year + 100;
    t->tm_mon  = mon - 1;
    t->tm_mday = day;
    t->tm_hour = hr;
    t->tm_min  = min;
    t->tm_sec  = sec;

    return true;
}

// -- Main loop ----------------------------------------------------------------

void XE310::loop() {
    _checkURCs();

    uint32_t now = XE310_MILLIS();

    if (now > _state.rssiUntil) {
        _updateRSSI();
        _state.rssiUntil = now + 30000;
    }

    if (now > _state.regUntil) {
        _updateRegistration();
        _state.regUntil = now + 10000;
    }
}

// -- AT command helpers (public) ----------------------------------------------

bool XE310::sendCommand(String cmd, String expected, uint32_t timeout) {
    _sendRaw(cmd.c_str());
    _sendRaw("\r\n");
    String resp = _readUntil(expected, timeout);
    return _contains(resp, expected.c_str());
}

String XE310::getCommand(String cmd, uint32_t timeout) {
    _sendRaw(cmd.c_str());
    _sendRaw("\r\n");
    return _readUntil(_makeStr("OK"), timeout);
}

String XE310::getCommand(String cmd, String filter, uint32_t timeout) {
    _sendRaw(cmd.c_str());
    _sendRaw("\r\n");
    String resp = _readUntil(_makeStr("OK"), timeout);
    if (_contains(resp, filter.c_str()))
        return _afterPrefix(resp, filter.c_str());
    return _makeStr("");
}

// -- Status / debug -----------------------------------------------------------

void XE310::logStatus() {
#ifdef XE310_PLATFORM_ARDUINO
    Serial.print("[xE310] RSSI=");
    Serial.print(_state.rssi);
    Serial.print(" REG=");
    Serial.println((int)_state.regStatus);
#else
    printf("[xE310] RSSI=%d  REG=%d\n", (int)_state.rssi, (int)_state.regStatus);
#endif
}

// -- Private helpers ----------------------------------------------------------

bool XE310::_isReady() {
    return sendCommand(_makeStr("AT"), _makeStr("OK"), 1000);
}

bool XE310::_waitReady(uint32_t timeout) {
    uint32_t deadline = XE310_MILLIS() + timeout;

    // Wait for "APP RDY" / "RDY" banner from module
    String banner = _readUntil(_makeStr("APP RDY"), timeout);
    if (_contains(banner, "APP RDY") || _contains(banner, "RDY"))
        XE310_DELAY(500);

    while (XE310_MILLIS() < deadline) {
        if (_isReady()) {
            _state.ready = true;
            return true;
        }
        XE310_DELAY(500);
    }
    return false;
}

bool XE310::_config() {
    // Echo off
    if (!sendCommand(_makeStr("ATE0"), _makeStr("OK"))) {
        XE310_DELAY(1000);
        sendCommand(_makeStr("ATE0"), _makeStr("OK"));
    }

    sendCommand(_makeStr("AT+CMEE=2"),              _makeStr("OK"));
    sendCommand(_makeStr("AT+CREG=2"),              _makeStr("OK"));
    sendCommand(_makeStr("AT+CEREG=2"),             _makeStr("OK"));
    sendCommand(_makeStr("AT#SCFGEXT=1,1,0,0,0,0"), _makeStr("OK"));

    _state.ready = true;
    return true;
}

bool XE310::_setRAT(uint8_t rat, uint16_t cops) {
    // AT#WS46: 12=GSM, 28=LTE-M, 29=NB-IoT, 25=Auto
    int ws46_mode;
    switch (rat) {
        case XE310_RAT_GSM:   ws46_mode = 12; break;
        case XE310_RAT_CATM1: ws46_mode = 28; break;
        case XE310_RAT_NBIOT: ws46_mode = 29; break;
        default:              ws46_mode = 25; break;
    }

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT#WS46=%d", ws46_mode);
    sendCommand(_makeStr(cmd), _makeStr("OK"), 10000);

    if (cops == 0) {
        sendCommand(_makeStr("AT+COPS=0"), _makeStr("OK"), 60000);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+COPS=1,2,\"%u\"", cops);
        sendCommand(_makeStr(cmd), _makeStr("OK"), 60000);
    }

    return true;
}

void XE310::_sendRaw(const char* data, uint16_t len) {
    _serial->write((const uint8_t*)data, len);
}

void XE310::_sendRaw(String data) {
    _serial->write((const uint8_t*)data.c_str(), (size_t)data.length());
}

String XE310::_readLine(uint32_t timeout) {
    String   line = _makeStr("");
    uint32_t deadline = XE310_MILLIS() + timeout;
    char     prev = 0;

    while (XE310_MILLIS() < deadline) {
        int c = _serial->read();
        if (c < 0) {
            XE310_DELAY(5);
            continue;
        }
        if (prev == '\r' && c == '\n') {
            if (line.length() > 0)
                line = _substr(line, 0, line.length() - 1);
            break;
        }
        line += (char)c;
        prev  = (char)c;
    }
    return line;
}

String XE310::_readUntil(String terminator, uint32_t timeout) {
    String   buf      = _makeStr("");
    uint32_t deadline = XE310_MILLIS() + timeout;

    while (XE310_MILLIS() < deadline) {
        int c = _serial->read();
        if (c >= 0) {
            buf += (char)c;
            if (_contains(buf, terminator.c_str()))
                break;
            if (_contains(buf, "ERROR"))
                break;
        } else {
            XE310_DELAY(2);
        }
    }
    return buf;
}

void XE310::_checkURCs() {
    String buf   = _makeStr("");
    int    tries = 0;

    while (_serial->available() > 0 && tries < 512) {
        int c = _serial->read();
        if (c >= 0) buf += (char)c;
        tries++;
    }
    if (buf.length() == 0) return;

    // Split on newlines and dispatch each line
    size_t start = 0;
    while (start < buf.length()) {
        size_t end = _findStr(_substr(buf, start), "\n");
        if (end == NPOS) {
            String line = _trim(_substr(buf, start));
            if (line.length() > 0) _parseURC(line);
            break;
        }
        String line = _trim(_substr(buf, start, end));
        if (line.length() > 0) _parseURC(line);
        start += end + 1;
    }
}

void XE310::_parseURC(String line) {
    if (line.length() == 0) return;

    // Socket ring / data available: SRING: <n>[,<rxlen>]
    if (_contains(line, "SRING:")) {
        size_t pos = _findStr(line, "SRING:");
        if (pos != NPOS) {
            int sockNum = atoi(_substr(line, pos + 7).c_str()) - 1;
            if (sockNum >= 0 && sockNum < XE310_MAX_SOCKETS)
                _sock[sockNum].dataPending = true;
        }
        return;
    }

    // Socket dropped
    if (_contains(line, "NO CARRIER")) {
        for (int i = 0; i < XE310_MAX_SOCKETS; i++) {
            if (_sock[i].connected) {
                _sock[i].connected = false;
                if (_tcpCloseCallback)
                    _tcpCloseCallback((uint8_t)i);
            }
        }
        return;
    }

    // Registration URC: +CEREG: <stat> or +CREG: <stat>
    if (_contains(line, "+CEREG:") || _contains(line, "+CREG:")) {
        const char* pfx = _contains(line, "+CEREG:") ? "+CEREG: " : "+CREG: ";
        String statStr  = _afterPrefix(line, pfx);
        size_t comma    = _findStr(statStr, ",");
        if (comma != NPOS)
            statStr = _substr(statStr, comma + 1);
        _state.regStatus = (int8_t)atoi(statStr.c_str());
        return;
    }

    // Incoming SMS: +CMTI: <mem>,<index>
    if (_contains(line, "+CMTI:") && _smsHandler != nullptr) {
        size_t pos = _findStr(line, ",");
        if (pos != NPOS) {
            uint8_t idx = (uint8_t)atoi(_substr(line, pos + 1).c_str());
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CMGR=%u", idx);
            String resp    = getCommand(_makeStr(cmd), 10000);
            String origin  = _makeStr("");
            String message = _makeStr("");

            size_t hdrPos = _findStr(resp, "+CMGR:");
            if (hdrPos != NPOS) {
                // Sender is in the second quoted field of the +CMGR header
                size_t base = hdrPos;
                for (int qi = 0; qi < 3; qi++) {
                    size_t q = _findStr(_substr(resp, base), "\"");
                    if (q == NPOS) break;
                    base += q + 1;
                }
                size_t qEnd = _findStr(_substr(resp, base), "\"");
                if (qEnd != NPOS)
                    origin = _substr(resp, base, qEnd);

                // Message body: line after the +CMGR header line
                size_t nl = _findStr(_substr(resp, hdrPos), "\n");
                if (nl != NPOS) {
                    message = _trim(_substr(resp, hdrPos + nl + 1));
                    size_t okPos = _findStr(message, "OK");
                    if (okPos != NPOS)
                        message = _trim(_substr(message, 0, okPos));
                }
            }
            _smsHandler(idx, origin, message);
        }
        return;
    }
}

void XE310::_updateRSSI() {
    String resp = getCommand(_makeStr("AT+CSQ"), _makeStr("+CSQ: "));
    if (resp.length() == 0) return;

    size_t comma = _findStr(resp, ",");
    int raw = atoi(_substr(resp, 0, comma != NPOS ? comma : resp.length()).c_str());

    _state.rssi = (raw == 99 || raw < 0)
                      ? 99
                      : (int16_t)(-113 + raw * 2);
}

void XE310::_updateRegistration() {
    String resp = getCommand(_makeStr("AT+CEREG?"), _makeStr("+CEREG: "));
    if (resp.length() == 0)
        resp = getCommand(_makeStr("AT+CREG?"), _makeStr("+CREG: "));
    if (resp.length() == 0) return;

    size_t comma   = _findStr(resp, ",");
    String statStr = (comma != NPOS) ? _substr(resp, comma + 1) : resp;
    _state.regStatus = (int8_t)atoi(_trim(statStr).c_str());
}

void XE310::_socketReadData(uint8_t socket) {
    if (socket >= XE310_MAX_SOCKETS) return;

    uint8_t  tmp[XE310_SOCKET_RXBUF];
    uint16_t len = tcpRecv(socket, tmp, XE310_SOCKET_RXBUF);
    if (len > 0) {
        if (len > XE310_SOCKET_RXBUF) len = XE310_SOCKET_RXBUF;
        memcpy(_sock[socket].rxBuf, tmp, len);
        _sock[socket].rxLen       = len;
        _sock[socket].dataPending = false;
    }
}

// -- Static wrappers ----------------------------------------------------------

bool XE310::_contains(const String& haystack, const char* needle) {
    return ::_contains(haystack, needle);
}

String XE310::_trim(String s) {
    return ::_trim(s);
}

String XE310::_afterPrefix(const String& line, const char* prefix) {
    return ::_afterPrefix(line, prefix);
}
