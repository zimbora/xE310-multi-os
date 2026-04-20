/*
 * basic.ino
 *
 * Arduino / ESP32 example: connect to the network and send a TCP message
 * using the xE310 Telit LTE-M / NB-IoT module.
 *
 * Wiring (ESP32 example):
 *   ESP32 TX2 (GPIO17) → xE310 RX
 *   ESP32 RX2 (GPIO16) → xE310 TX
 *   ESP32 GND          → xE310 GND
 *   ESP32 3V3 / 5V     → xE310 VCC (check module voltage requirements)
 *   ESP32 GPIO4        → xE310 PWRKEY (optional)
 */

#include <Arduino.h>
#include "xE310.hpp"
#include "SerialArduino.hpp"

// ── Configuration ─────────────────────────────────────────────────────────────

#define MODEM_BAUD     115200
#define MODEM_RX_PIN   16
#define MODEM_TX_PIN   17
#define MODEM_PWRKEY   4     // -1 if not connected

#define APN            "internet"   // Replace with your operator APN
#define APN_USER       ""
#define APN_PASS       ""

#define TCP_HOST       "tcpbin.com"
#define TCP_PORT       4242

// ── Globals ───────────────────────────────────────────────────────────────────

SerialArduino modemSerial(&Serial2);
XE310         modem(&modemSerial, MODEM_PWRKEY);

// ── Arduino setup ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("[demo] Starting xE310 example...");

    // Open the modem serial port
    if (!modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN)) {
        Serial.println("[demo] Failed to open serial port!");
        while (true) delay(1000);
    }

    // Initialise the module (power-cycle + base AT config)
    Serial.println("[demo] Initialising module...");
    if (!modem.init(XE310_RAT_CATM1, 0)) {
        Serial.println("[demo] Module init failed!");
        while (true) delay(1000);
    }

    // Print module info
    Serial.print("[demo] IMEI: ");
    Serial.println(modem.getIMEI());
    Serial.print("[demo] Model: ");
    Serial.println(modem.getModel());
    Serial.print("[demo] Firmware: ");
    Serial.println(modem.getFirmwareVersion());

    // Wait for network registration
    Serial.println("[demo] Waiting for network registration...");
    uint32_t deadline = millis() + 60000;
    while (!modem.isRegistered() && millis() < deadline) {
        modem.loop();
        delay(1000);
        Serial.print(".");
    }
    Serial.println();

    if (!modem.isRegistered()) {
        Serial.println("[demo] Registration timed out!");
        while (true) delay(1000);
    }

    Serial.print("[demo] RSSI: ");
    Serial.print(modem.getRSSI());
    Serial.println(" dBm");

    // Set up APN and activate data context
    Serial.println("[demo] Configuring APN...");
    if (!modem.setupAPN(1, APN, APN_USER, APN_PASS)) {
        Serial.println("[demo] APN setup failed!");
        while (true) delay(1000);
    }

    Serial.println("[demo] Activating PDP context...");
    if (!modem.activatePDP(1)) {
        Serial.println("[demo] PDP activation failed!");
        while (true) delay(1000);
    }

    Serial.print("[demo] IP address: ");
    Serial.println(modem.getIP());

    // Open a TCP connection and send a message
    Serial.println("[demo] Connecting to " TCP_HOST "...");
    if (!modem.tcpConnect(0, TCP_HOST, TCP_PORT, 1, 30000)) {
        Serial.println("[demo] TCP connect failed!");
        while (true) delay(1000);
    }

    Serial.println("[demo] Sending data...");
    const char* msg = "Hello from xE310!\r\n";
    if (!modem.tcpSend(0, msg)) {
        Serial.println("[demo] TCP send failed!");
    } else {
        Serial.println("[demo] Data sent!");
    }

    // Wait a moment for the echo
    delay(2000);

    uint8_t rxBuf[256];
    uint16_t rxLen = modem.tcpRecv(0, rxBuf, sizeof(rxBuf) - 1);
    if (rxLen > 0) {
        rxBuf[rxLen] = 0;
        Serial.print("[demo] Received: ");
        Serial.println((char*)rxBuf);
    }

    modem.tcpClose(0);
    Serial.println("[demo] Done.");
}

// ── Arduino loop ──────────────────────────────────────────────────────────────

void loop() {
    modem.loop();
    delay(100);
}
