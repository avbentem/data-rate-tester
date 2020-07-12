#ifndef DATA_RATE_TESTER_CONFIG_H
#define DATA_RATE_TESTER_CONFIG_H

#include "Arduino.h"
#include "lmic/oslmic_types.h"
#include "hal/hal.h"

// ==========
// ========== LoRaWAN ABP configuration
// ==========

// LoRaWAN ABP secret NwkSKey, network session key. This should be in big-endian (aka MSB).
static const PROGMEM u1_t NWKSKEY[16] =
  {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// LoRaWAN ABP secret AppSKey, application session key. This should be in big-endian (aka MSB).
static const PROGMEM u1_t APPSKEY[16] =
  {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// LoRaWAN ABP DevAddr, device address. This should be in big-endian (aka MSB). For EU868 on TTN,
// this starts with 0x26011; see https://www.thethingsnetwork.org/docs/lorawan/addressing.html
static const u4_t DEVADDR = 0x26011000;

// ==========
// ========== Heltec WiFi LoRa 32 board (first release) configuration
// ==========

// Pin mapping; see https://github.com/mcci-catena/arduino-lmic#manual-configuration
static const lmic_pinmap LMIC_PINS = {
    .nss = SS, // active-low SPI slave-select; pin 18
    .rxtx = LMIC_UNUSED_PIN, // rx/tx control
    .rst = RST_LoRa, // reset; pin 14
    .dio = {DIO0, DIO1, DIO2} // digital input/output; pins 26, 33, 32
};

static const uint8_t OLED_ADDRESS = 0x3c;

// Button "PROG" on board, also connected to pin 0 and GND.
static const uint8_t STATE_BUTTON = KEY_BUILTIN;

#endif // DATA_RATE_TESTER_CONFIG_H
