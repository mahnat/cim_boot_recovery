/**
 * @file       BlynkSimpleSerial.h
 * @author     Volodymyr Shymanskyy
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2015 Volodymyr Shymanskyy
 * @date       Jan 2015
 * @brief
 *
 */

#ifndef BlynkSimpleSerial_h
#define BlynkSimpleSerial_h

#ifndef BLYNK_INFO_CONNECTION
#define BLYNK_INFO_CONNECTION "Serial"
#endif

#include <BlynkApiMbed.h>
#include <Blynk/BlynkProtocol.h>

class BlynkTransportSerial
{
public:
    BlynkTransportSerial()
        : serial(NULL), conn(0)
    {}

    // IP redirect not available
    void begin(char BLYNK_UNUSED *h, uint16_t BLYNK_UNUSED p) {}

    void begin(Serial& s) {
        serial = &s;
    }

    bool connect() {
        BLYNK_LOG1(BLYNK_F("Connecting..."));
        return conn = true;
    }
    void disconnect() { conn = false; }

    size_t read(void* buf, size_t len) {
        return fread(buf, 1, len, *serial);
    }
    size_t write(const void* buf, size_t len) {
        return fwrite(buf, 1, len, *serial);
    }

    bool connected() { return conn; }
    int available() { return serial->readable(); }

protected:
    Serial* serial;
    bool    conn;
};

class BlynkSerial
    : public BlynkProtocol<BlynkTransportSerial>
{
    typedef BlynkProtocol<BlynkTransportSerial> Base;
public:
    BlynkSerial(BlynkTransportSerial& transp)
        : Base(transp)
    {}

    void config(Serial&     serial,
                const char* auth)
    {
        Base::begin(auth);
        this->conn.begin(serial);
    }

    void begin(Serial& serial, const char* auth) {
        config(serial, auth);
        while(this->connect() != true) {}
    }
};

static BlynkTransportSerial _blynkTransport;
BlynkSerial Blynk(_blynkTransport);

#include <BlynkWidgets.h>

#endif
