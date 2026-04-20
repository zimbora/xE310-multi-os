/*
 * main.cpp
 *
 * Linux / macOS example: connect to the network and send a TCP message
 * using the xE310 Telit LTE-M / NB-IoT module via a USB-serial adapter
 * or built-in UART.
 *
 * Build:
 *   mkdir build && cd build
 *   cmake ..
 *   make
 *   ./xE310_example /dev/ttyUSB0
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

#include "xE310.hpp"
#include "SerialPosix.hpp"

// ── Configuration ─────────────────────────────────────────────────────────────

static const char* APN      = "internet";   // Replace with your operator APN
static const char* APN_USER = "";
static const char* APN_PASS = "";

static const char* TCP_HOST = "tcpbin.com";
static const uint16_t TCP_PORT = 4242;

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* device = (argc > 1) ? argv[1] : "/dev/ttyUSB0";

    printf("[demo] Opening %s\n", device);

    SerialPosix serial(device);
    XE310       modem(&serial); // no PWRKEY on desktop

    // Open serial port
    if (!serial.begin(115200)) {
        fprintf(stderr, "[demo] Failed to open %s\n", device);
        return 1;
    }

    // Initialise the module
    printf("[demo] Initialising module...\n");
    if (!modem.init(XE310_RAT_CATM1, 0)) {
        fprintf(stderr, "[demo] Module init failed!\n");
        return 1;
    }

    // Print module info
    printf("[demo] IMEI     : %s\n", modem.getIMEI().c_str());
    printf("[demo] Model    : %s\n", modem.getModel().c_str());
    printf("[demo] Firmware : %s\n", modem.getFirmwareVersion().c_str());

    // Wait for network registration (up to 60 s)
    printf("[demo] Waiting for network registration...\n");
    uint32_t deadline = _xe310_millis() + 60000;
    while (!modem.isRegistered() && _xe310_millis() < deadline) {
        modem.loop();
        usleep(1000 * 1000);
        printf(".");
        fflush(stdout);
    }
    printf("\n");

    if (!modem.isRegistered()) {
        fprintf(stderr, "[demo] Registration timed out!\n");
        return 1;
    }

    printf("[demo] RSSI: %d dBm\n", (int)modem.getRSSI());

    // Configure APN and activate PDP context
    printf("[demo] Configuring APN '%s'...\n", APN);
    if (!modem.setupAPN(1, APN, APN_USER, APN_PASS)) {
        fprintf(stderr, "[demo] APN setup failed!\n");
        return 1;
    }

    printf("[demo] Activating PDP context...\n");
    if (!modem.activatePDP(1)) {
        fprintf(stderr, "[demo] PDP activation failed!\n");
        return 1;
    }

    printf("[demo] IP address: %s\n", modem.getIP().c_str());

    // TCP connection
    printf("[demo] Connecting to %s:%u...\n", TCP_HOST, TCP_PORT);
    if (!modem.tcpConnect(0, TCP_HOST, TCP_PORT, 1, 30000)) {
        fprintf(stderr, "[demo] TCP connect failed!\n");
        return 1;
    }

    printf("[demo] Sending data...\n");
    const char* msg = "Hello from xE310!\r\n";
    if (!modem.tcpSend(0, msg)) {
        fprintf(stderr, "[demo] TCP send failed!\n");
    } else {
        printf("[demo] Data sent!\n");
    }

    // Wait for echo
    usleep(2000 * 1000);

    uint8_t rxBuf[256];
    uint16_t rxLen = modem.tcpRecv(0, rxBuf, sizeof(rxBuf) - 1);
    if (rxLen > 0) {
        rxBuf[rxLen] = 0;
        printf("[demo] Received: %s\n", (char*)rxBuf);
    }

    modem.tcpClose(0);
    printf("[demo] Done.\n");
    return 0;
}
