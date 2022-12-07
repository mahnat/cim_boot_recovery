/**
 * @file       BlynkSimpleRedBearLab_BLE_Nano.h
 * @author     Volodymyr Shymanskyy
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2015 Volodymyr Shymanskyy
 * @date       May 2016
 * @brief
 *
 */

#ifndef BlynkSimpleRedBearLab_BLE_Nano_h
#define BlynkSimpleRedBearLab_BLE_Nano_h

#ifndef BLYNK_INFO_CONNECTION
#define BLYNK_INFO_CONNECTION "MicroBit"
#endif

#define BLYNK_SEND_ATOMIC
#define BLYNK_SEND_CHUNK 20
#define BLYNK_SEND_THROTTLE 20

#include <BlynkApiMbed.h>
#include <Blynk/BlynkProtocol.h>
#include <utility/BlynkFifo.h>
#include <ble/BLE.h>

/*
 * The Nordic UART Service
 */
static const uint8_t uart_base_uuid[]     = {0x71, 0x3D, 0, 0, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};
static const uint8_t uart_base_uuid_rev[] = {0x1E, 0x94, 0x8D, 0xF1, 0x48, 0x31, 0x94, 0xBA, 0x75, 0x4C, 0x3E, 0x50, 0, 0, 0x3D, 0x71};
static const uint8_t uart_tx_uuid[]       = {0x71, 0x3D, 0, 3, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};
static const uint8_t uart_rx_uuid[]       = {0x71, 0x3D, 0, 2, 0x50, 0x3E, 0x4C, 0x75, 0xBA, 0x94, 0x31, 0x48, 0xF1, 0x8D, 0x94, 0x1E};

#define TXRX_BUF_LEN 20
uint8_t txPayload[TXRX_BUF_LEN] = {0,};
uint8_t rxPayload[TXRX_BUF_LEN] = {0,};

GattCharacteristic  txCharacteristic (uart_tx_uuid, txPayload, 1, TXRX_BUF_LEN,
                                      GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE |
                                      GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE_WITHOUT_RESPONSE);

GattCharacteristic  rxCharacteristic (uart_rx_uuid, rxPayload, 1, TXRX_BUF_LEN,
                                      GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);

GattCharacteristic *uartChars[] = {&txCharacteristic, &rxCharacteristic};

GattService         uartService(uart_base_uuid, uartChars, sizeof(uartChars) / sizeof(GattCharacteristic*));

class BlynkTransportMicroBit
{
public:
    BlynkTransportMicroBit()
        : mConn (false)
    {}

    // IP redirect not available
    void begin(char* h, uint16_t p) {}

    void begin(BLEDevice& bleDevice) {
        ble = &bleDevice;

        ble->gattServer().addService(uartService);
        ble->gattServer().onDataWritten(this, &BlynkTransportMicroBit::writeCallback);
        ble->gattServer().onDataSent(this, &BlynkTransportMicroBit::sentCallback);

        // Setup advertising
        ble->gap().accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LIST_128BIT_SERVICE_IDS,
                                              uart_base_uuid_rev, sizeof(uart_base_uuid));
    }

    bool connect() {
        mBuffRX.clear();
        return mConn = true;
    }

    void disconnect() {
        mConn = false;
    }

    bool connected() {
        return mConn;
    }

    size_t read(void* buf, size_t len) {
        millis_time_t start = BlynkMillis();
        while (BlynkMillis() - start < BLYNK_TIMEOUT_MS) {
            if (available() < len) {
                ble->waitForEvent();
            } else {
                break;
            }
        }
        noInterrupts();
        size_t res = mBuffRX.get((uint8_t*)buf, len);
        interrupts();
        return res;
    }

    size_t write(const void* buf, size_t len) {
        ble->updateCharacteristicValue(rxCharacteristic.getValueAttribute().getHandle(), (uint8_t*)buf, len);
        return len;
    }

    size_t available() {
        noInterrupts();
        size_t rxSize = mBuffRX.size();
        interrupts();
        return rxSize;
    }

private:

    void writeCallback(const GattWriteCallbackParams *params)
    {
        noInterrupts();
        //BLYNK_DBG_DUMP(">> ", params->data, params->len);
        mBuffRX.put(params->data, params->len);
        interrupts();
    }

    void sentCallback(unsigned count)
    {
        //Serial.print("SENT: ");
        //Serial.println(count);
    }

private:
    bool mConn;
    BLEDevice* ble;

    BlynkFifo<uint8_t, BLYNK_MAX_READBYTES*2> mBuffRX;
};

class BlynkMicroBit
    : public BlynkProtocol<BlynkTransportMicroBit>
{
    typedef BlynkProtocol<BlynkTransportMicroBit> Base;
public:
    BlynkMicroBit(BlynkTransportMicroBit& transp)
        : Base(transp)
    {}

    void begin(BLEDevice& ble, const char* auth)
    {
        Base::begin(auth);
        state = DISCONNECTED;
        conn.begin(ble);
    }
};

static BlynkTransportMicroBit _blynkTransport;
BlynkMicroBit Blynk(_blynkTransport);

#include <BlynkWidgets.h>

#endif
