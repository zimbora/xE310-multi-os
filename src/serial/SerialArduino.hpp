#pragma once

/*
 * SerialArduino.hpp
 *
 * Arduino / ESP32 implementation of ISerial using HardwareSerial.
 * This is a header-only implementation so Arduino sketches can include
 * it directly without a separate compilation step.
 */

#ifdef ARDUINO

#include <Arduino.h>
#include "ISerial.hpp"

class SerialArduino : public ISerial {
public:
    /*
     * @serial   – pointer to a HardwareSerial instance (e.g. &Serial2)
     */
    explicit SerialArduino(HardwareSerial* serial) : _serial(serial) {}

    bool begin(uint32_t baudrate) override {
        _serial->begin(baudrate);
        return true;
    }

    /*
     * Overload that also sets serial config and RX/TX pins (ESP32).
     */
    bool begin(uint32_t baudrate, uint32_t config, int8_t rx_pin, int8_t tx_pin) {
        _serial->begin(baudrate, config, rx_pin, tx_pin);
        return true;
    }

    void end() override {
        _serial->end();
    }

    int available() override {
        return _serial->available();
    }

    int read() override {
        return _serial->read();
    }

    size_t read(uint8_t* buf, size_t len) override {
        return _serial->readBytes(buf, len);
    }

    size_t write(const uint8_t* buf, size_t len) override {
        return _serial->write(buf, len);
    }

    size_t write(const char* str) override {
        return _serial->print(str);
    }

    void flush() override {
        _serial->flush();
    }

private:
    HardwareSerial* _serial;
};

#endif // ARDUINO
