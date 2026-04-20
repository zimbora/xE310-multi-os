#pragma once

/*
 * xE310.hpp
 *
 * Cross-platform C++ library for Telit xE310 LTE-M / NB-IoT modules.
 *
 * Supported platforms:
 *   - Arduino / ESP32  (via SerialArduino)
 *   - Linux / macOS    (via SerialPosix)
 *   - Windows          (via SerialWindows)
 *
 * Usage:
 *   1. Create a platform-specific serial object (SerialArduino, SerialPosix,
 *      or SerialWindows).
 *   2. Construct an XE310 instance, passing the serial object.
 *   3. Call begin() to open the port, then init() to configure the module.
 *   4. Call loop() periodically from your main loop.
 */

#include "platform.hpp"
#include "serial/ISerial.hpp"
#include <stdint.h>
#include <time.h>

// ── Constants ──────────────────────────────────────────────────────────────────

// Radio access technology
#define XE310_RAT_AUTO    0
#define XE310_RAT_GSM     1
#define XE310_RAT_CATM1   2
#define XE310_RAT_NBIOT   3

// Network registration states (AT+CREG / AT+CEREG)
#define XE310_REG_NOT_REGISTERED   0
#define XE310_REG_REGISTERED_HOME  1
#define XE310_REG_SEARCHING        2
#define XE310_REG_DENIED           3
#define XE310_REG_UNKNOWN          4
#define XE310_REG_ROAMING          5

// Maximum number of simultaneous sockets (xE310 supports up to 6)
#define XE310_MAX_SOCKETS    6
// Maximum number of PDP contexts
#define XE310_MAX_CONTEXTS   2
// Default AT command timeout (ms)
#define XE310_DEFAULT_TIMEOUT  5000
// Receive buffer size per socket
#define XE310_SOCKET_RXBUF    1500

// ── xE310 class ───────────────────────────────────────────────────────────────

class XE310 {
public:

    // ── Construction ──────────────────────────────────────────────────────────

    /*
     * @serial  – Pointer to an ISerial implementation (not owned by this class).
     *            Must remain valid for the lifetime of this XE310 instance.
     * @pwkey   – Optional GPIO pin number to toggle module power.
     *            Pass -1 if power is always on or controlled externally.
     */
    explicit XE310(ISerial* serial, int8_t pwkey = -1);

    // ── Port management ───────────────────────────────────────────────────────

    /*
     * Open the serial port at @baudrate (default 115200).
     * Must be called before any other method.
     */
    bool begin(uint32_t baudrate = 115200);

    /*
     * Close the serial port.
     */
    void end();

    // ── Module initialisation ─────────────────────────────────────────────────

    /*
     * Full module initialisation:
     *   - Power-cycles the module (if pwkey is configured)
     *   - Waits for the module to be ready
     *   - Sets echo off, error format, and radio technology
     *
     * @rat   – Radio access technology (XE310_RAT_*)
     * @cops  – Numeric operator code, or 0 for automatic selection
     *
     * Returns true on success.
     */
    bool init(uint8_t rat = XE310_RAT_AUTO, uint16_t cops = 0);

    // ── Power management ──────────────────────────────────────────────────────

    bool powerOn();
    bool powerOff();
    bool powerCycle();
    bool softReset();    // AT#REBOOT

    // ── Module identification ─────────────────────────────────────────────────

    String getIMEI(uint32_t timeout = XE310_DEFAULT_TIMEOUT);
    String getICCID(uint32_t timeout = XE310_DEFAULT_TIMEOUT);
    String getIMSI(uint32_t timeout = XE310_DEFAULT_TIMEOUT);
    String getModel(uint32_t timeout = XE310_DEFAULT_TIMEOUT);
    String getManufacturer(uint32_t timeout = XE310_DEFAULT_TIMEOUT);
    String getFirmwareVersion(uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    // ── Network ───────────────────────────────────────────────────────────────

    /*
     * Returns true when the module is registered on a network
     * (home or roaming).
     */
    bool isRegistered();

    /*
     * Raw CREG / CEREG registration status code (XE310_REG_*).
     */
    int8_t getRegistrationStatus();

    /*
     * Signal quality in dBm. Returns 99 if unknown / not readable.
     */
    int16_t getRSSI();

    /*
     * Current operator name.
     */
    String getOperator(uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    // ── PDP context (data connection) ─────────────────────────────────────────

    /*
     * Configure a PDP context.
     *
     * @cid      – Context identifier (1..XE310_MAX_CONTEXTS)
     * @apn      – APN string
     * @username – Username (may be empty)
     * @password – Password (may be empty)
     */
    bool setupAPN(uint8_t cid, String apn, String username = "", String password = "");

    /*
     * Activate (bring up) a PDP context.
     */
    bool activatePDP(uint8_t cid = 1);

    /*
     * Deactivate a PDP context.
     */
    bool deactivatePDP(uint8_t cid = 1);

    /*
     * Return the IP address assigned to a context, or empty string.
     */
    String getIP(uint8_t cid = 1, uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    /*
     * Returns true when the context has an IP address.
     */
    bool isContextActive(uint8_t cid = 1);

    // ── TCP sockets ───────────────────────────────────────────────────────────

    /*
     * Open a TCP connection.
     *
     * @socket  – Socket number (0..XE310_MAX_SOCKETS-1)
     * @host    – Remote host (IP or hostname)
     * @port    – Remote port
     * @cid     – PDP context to use
     * @timeout – Connection timeout in ms
     */
    bool tcpConnect(uint8_t socket, String host, uint16_t port,
                    uint8_t cid = 1, uint32_t timeout = 30000);

    /*
     * Close a TCP connection.
     */
    bool tcpClose(uint8_t socket);

    /*
     * Send data over an open TCP socket.
     */
    bool tcpSend(uint8_t socket, const uint8_t* data, uint16_t len);

    /*
     * Convenience overload for text.
     */
    bool tcpSend(uint8_t socket, const char* text);

    /*
     * Read received data from a socket into @data (up to @maxLen bytes).
     * Returns the number of bytes copied.
     */
    uint16_t tcpRecv(uint8_t socket, uint8_t* data, uint16_t maxLen,
                     uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    /*
     * Returns true if the socket is currently connected.
     */
    bool tcpIsConnected(uint8_t socket);

    /*
     * Register a callback invoked when a socket is unexpectedly closed.
     */
    void tcpSetCloseCallback(void(*cb)(uint8_t socket));

    // ── UDP sockets ───────────────────────────────────────────────────────────

    bool udpOpen(uint8_t socket, uint16_t localPort, uint8_t cid = 1);
    bool udpClose(uint8_t socket);
    bool udpSend(uint8_t socket, String host, uint16_t port,
                 const uint8_t* data, uint16_t len);

    // ── DNS ───────────────────────────────────────────────────────────────────

    /*
     * Resolve a hostname to an IP address string.
     * Returns empty string on failure.
     */
    String dnsResolve(String hostname, uint32_t timeout = 10000);

    // ── Ping ──────────────────────────────────────────────────────────────────

    /*
     * Send ICMP ping. Returns round-trip time in ms, or -1 on failure.
     */
    int32_t ping(String host, uint8_t retries = 4);

    // ── SMS ───────────────────────────────────────────────────────────────────

    /*
     * Send an SMS.
     *
     * @number  – Destination phone number (international format, e.g. "+351...")
     * @message – Text message (max 160 chars for GSM7)
     */
    bool sendSMS(String number, String message);

    /*
     * Register a callback for incoming SMS messages.
     * The callback receives (index, origin, message).
     */
    bool setSMSHandler(void(*handler)(uint8_t index, String origin, String message));

    // ── Clock ─────────────────────────────────────────────────────────────────

    /*
     * Get the network clock.
     * Fills @t with the current time (UTC).
     * Returns true on success.
     */
    bool getNetworkTime(struct tm* t);

    // ── Main loop ─────────────────────────────────────────────────────────────

    /*
     * Call this frequently from your main loop.
     * Handles incoming URCs (unsolicited result codes) and keeps internal
     * state up-to-date (signal quality, registration, socket data, SMS, etc.).
     */
    void loop();

    // ── Low-level AT helpers (public for advanced use) ────────────────────────

    /*
     * Send an AT command and wait for an expected response string.
     * Returns true if @expected is found before timeout.
     */
    bool sendCommand(String cmd, String expected,
                     uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    /*
     * Send an AT command and return the full response text.
     */
    String getCommand(String cmd, uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    /*
     * Send an AT command and return the text after @filter.
     */
    String getCommand(String cmd, String filter,
                      uint32_t timeout = XE310_DEFAULT_TIMEOUT);

    // ── Status / debug ────────────────────────────────────────────────────────

    void logStatus();

private:

    // ── Internal types ────────────────────────────────────────────────────────

    struct Context {
        bool   active;
        bool   connected;
        char   apn[64];
        char   ip[16];
        uint8_t cid;
    };

    struct Socket {
        bool     connected;
        bool     dataPending;
        uint8_t  contextID;
        uint16_t rxLen;
        uint8_t  rxBuf[XE310_SOCKET_RXBUF];
    };

    struct ModuleState {
        bool    ready;
        bool    simReady;
        uint8_t rat;
        uint16_t cops;
        int16_t rssi;
        int8_t  regStatus;        // CEREG / CREG stat field
        uint32_t rssiUntil;       // next rssi refresh timestamp
        uint32_t regUntil;        // next registration check
    };

    struct SMS {
        bool    used;
        uint8_t index;
        char    origin[20];
        char    message[256];
    };

    // ── Members ───────────────────────────────────────────────────────────────

    ISerial*    _serial;
    int8_t      _pwkey;

    ModuleState _state;
    Context     _ctx[XE310_MAX_CONTEXTS];
    Socket      _sock[XE310_MAX_SOCKETS];

    void(*_tcpCloseCallback)(uint8_t) = nullptr;
    void(*_smsHandler)(uint8_t, String, String) = nullptr;

    // ── Internal helpers ──────────────────────────────────────────────────────

    bool _isReady();
    bool _waitReady(uint32_t timeout = 15000);

    bool _config();
    bool _setRAT(uint8_t rat, uint16_t cops);

    void _sendRaw(const char* data, uint16_t len);
    void _sendRaw(String data);

    String _readLine(uint32_t timeout = 2000);
    String _readUntil(String terminator, uint32_t timeout = 5000);

    void _checkURCs();
    void _parseURC(String line);

    void _updateRSSI();
    void _updateRegistration();

    void _socketReadData(uint8_t socket);

    // ── Helpers ───────────────────────────────────────────────────────────────

    static bool _contains(const String& haystack, const char* needle);
    static String _trim(String s);

    // Extract the part of @line after @prefix
    static String _afterPrefix(const String& line, const char* prefix);
};
