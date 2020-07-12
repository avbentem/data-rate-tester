/**
 * Test LoRaWAN uplinks by quickly cycling through different data rates, (ab)using the EU868 maximum
 * duty cycle, optionally using confirmed uplinks to also test downlinks (but without actually
 * retrying if no confirmation is received), and always using the maximum transmission power.
 *
 * This code is specific for EU868 on The Things Network.
 */
#include "SPI.h"
#include "OneButton.h"
#include "lmic.h"
#include "hal/hal.h"
#include "config.h"
#include "display.h"
#include "logger.h"

bool isConfirmed = false;
bool isAutoDataRate = true;

// The order in which to cycle through data rates if isAutoDataRate == true. SF7 is repeated a few
// times in between the slower data rates as well, to limit waiting time. Assume EU868:
static const uint8_t dataRates[] = {DR_SF7, DR_SF8,  DR_SF9, DR_SF7,  DR_SF12, DR_SF7,
                                    DR_SF7, DR_SF11, DR_SF7, DR_SF10, DR_SF7};

// The running index of the next data rate in array `dataRates`, if isAutoDataRate == true
int8_t dataRateIdx = -1;
uint8_t dataRate;

// Re-use the state as used by Display; we could also have defined our own instead
State state = STATE_NOP;

// After TX, LMIC.seqnoUp will already be increased while still awaiting the receive windows
uint32_t seqnoUp = LMIC.seqnoUp;
uint32_t txFreq;
ostime_t rx1time;
ostime_t rx2time;

static void toggleConfirmed() {
  isConfirmed = !isConfirmed;
  display.setIsConfirmedUplink(isConfirmed);
}

static void nextDataRate() {
  if (isAutoDataRate) {
    dataRateIdx = (dataRateIdx + 1) % (int)(sizeof(dataRates) / sizeof(dataRates[0]));
    dataRate = dataRates[dataRateIdx];
  } else {
    // Assume EU868, DR_SF7 thru DR_SF12
    dataRateIdx = (dataRateIdx + 1) % 6;
    dataRate = DR_SF7 - dataRateIdx;
  }

  // Assume EU868
  display.setTxSpreadingFactor(12 - dataRate);
}

static void toggleAutoDataRate() {
  isAutoDataRate = !isAutoDataRate;
  display.setIsFixedDataRate(!isAutoDataRate);
  // Changing the data rate for a canceled/delayed TX may make LMIC select another frequency when
  // scheduling the transmission again. We cannot tell at this point.
  dataRateIdx = -1;
  nextDataRate();
}

/**
 * Update the current state, using the internals of the LMIC timing.
 */
void updateStateAndDisplay() {
  ostime_t now = os_getTime();

  // Even if we canceled a scheduled transmission, LMIC.txend will still be set
  if (state == STATE_RXDONE && LMIC.txend - now > 0) {
    state = STATE_WAITING;
    int32_t targetMs = osticks2ms(LMIC.txend);
    Logger::logf("TX at %d ticks/%.1f sec", LMIC.txend, targetMs / 1000.0);
    display.startWaitTx(targetMs);
  }

  // The very first transmission has no waiting time
  if ((state == STATE_NOP || state == STATE_WAITING) && (now - LMIC.txend > 0)) {
    state = STATE_TX;
    // The transmission is also logged in do_send, but this can help debugging timing problems, like
    // when this is logged after seeing EV_TXCOMPLETE
    Logger::log("TX");
    display.startTx();
  }

  // When transmission starts, LMIC.txend will temporarily be zero; when done, it will be set to the
  // exact time transmission completed
  if (state == STATE_TX && (LMIC.rxtime - now > 0)) {
    state = STATE_RX1;
    rx1time = LMIC.rxtime;
    int32_t targetMs = osticks2ms(rx1time);

    // LMIC.seqnoUp has already been increased; we may use LMIC.datarate and LMIC.freq as long as
    // RX1 has not started
    Logger::logf(
        "TX done: seqnoUp=%d; SF=%d; freq=%.1f; txend=%d ticks/%.1f sec; RX1 at %d ticks/%.1f sec",
        seqnoUp, 12 - LMIC.datarate, LMIC.freq / 1E6, LMIC.txend, osticks2ms(LMIC.txend) / 1000.0,
        rx1time, targetMs / 1000.0);
    display.startWaitRx1(targetMs);
  }

  // If LMIC has set a new value for LMIC.rxtime, then it has completed RX1
  if (state == STATE_RX1 && (LMIC.rxtime - rx1time > 0)) {
    state = STATE_RX2;
    rx2time = LMIC.rxtime;
    int32_t targetMs = osticks2ms(rx2time);
    Logger::logf("RX1 done: RX2 at %d ticks/%.1f sec", rx2time, targetMs / 1000.0);
    display.startWaitRx2(targetMs);
  }

  // RX2 is skipped if a downlink is received in RX1
  if ((state == STATE_RX1 || state == STATE_RX2) && (LMIC.opmode & OP_TXRXPEND) == 0) {
    // We could also set this in the LMIC onEvent handler
    state = STATE_RXDONE;
    Logger::log("RX done");
    display.stop();
  }
}

static osjob_t sendjob;

/**
 * Schedule a new transmission, immediately canceling and re-scheduling if LMIC did not send right
 * away, to allow for changing the transmission parameters while awaiting the duty cycle limit.
 */
void do_send(__unused osjob_t *j) {
  // Check if there is not a current TX/RX job running; should not happen
  if (LMIC.opmode & OP_TXRXPEND) {
    Logger::log("ERROR: OP_TXRXPEND, not scheduling new transmission");
    return;
  }

  if (isAutoDataRate && state != STATE_WAITING) {
    nextDataRate();
    Logger::logf("Next auto data rate index=%d", dataRateIdx);
  }

  // Data rate and transmission power
  LMIC_setDrTxpow(dataRate, 14);

  // Dummy data; this is redundant (already known in TTN Console/MQTT). Assume EU868; SF in BCD,
  // binary-coded decimal, so: 0x07, 08, 09, 10, 11, 12 rather than 0x07 thru 0x0C.
  uint8_t data[1];
  u1_t sf = 12 - dataRate;
  data[0] = (sf / 10u) << 4 | (sf % 10u);

  // Save BEFORE scheduling, as it will change immediately after transmission has completed (and
  // scheduling may actually yield an immediate transmission) while we want to show the uplink
  // counter while awaiting the RX1 and RX2 as well. We could also capture this next value in the
  // EV_TXCOMPLETE event.
  seqnoUp = LMIC.seqnoUp;
  display.setTxCount(seqnoUp);

  // Send or enqueue an uplink on port number matching SF, unconfirmed. As this has been scheduled
  // in the TX_COMPLETE event, without taking the maximum duty cycle into account, LMIC might delay
  // the actual transmission. "Strict" to ensure LMIC does not adjust the data rate if the payload
  // would be too long for the given data rate (which, of course, will not happen here).
  LMIC_setTxData2_strict(sf, data, sizeof(data), isConfirmed ? 1 : 0);

  // Disable the retries for confirmed uplinks by fooling LMIC into thinking it has already done
  // all of its 8 attempts. This also ensures LMIC will not retry with a slower data rate. See
  // https://github.com/mcci-catena/arduino-LMIC/blob/v3.2.0/src/LMIC/LMIC.c#L2285 and
  // https://www.thethingsnetwork.org/forum/t/2902/6
  if (isConfirmed) {
    LMIC.txCnt = TXCONF_ATTEMPTS;
  }

  // LMIC.freq will only be set when TX begins, and changes when RX2 starts. LMIC.txChnl is
  // documented as "channel for next TX" and is in fact not changed until the receive windows have
  // been handled.
  txFreq = LMIC.channelFreq[LMIC.txChnl];
  display.setTxFreq(txFreq);

  int waitTicks = LMIC.txend - os_getTime();

  if (waitTicks > 0) {
    // Now that we know how many ticks to wait: cancel, and re-schedule to allow for changing the
    // settings (like SF or toggling confirmed uplinks) just before the transmission is allowed.
    // This may include a 3-5 second safety zone. After canceling, the next TX for the very same
    // DR will use the same channel. But when using a different DR then LMIC may also select
    // another channel.
    float waitSeconds = osticks2ms(waitTicks) / 1000.0;
    Logger::logf(
        "Cannot send yet, rescheduling: seqnoUp=%d; SF=%d; freq=%.1f; wait=%lu ticks/%.1f sec",
        LMIC.seqnoUp, 12 - LMIC.datarate, txFreq / 1E6, waitTicks, waitSeconds);

    LMIC_clrTxData();
    os_setTimedCallback(&sendjob, os_getTime() + waitTicks, do_send);
    return;
  }

  String txPayload = "";
  for (int i = 0; i < LMIC.dataLen; i++) {
    if (LMIC.frame[i] < 0x10) {
      txPayload += "0";
    }
    txPayload += String(LMIC.frame[i], HEX);
  }

  // We know that LMIC will have started transmission right away; in fact it will already have fired
  // EV_TXSTART and have increased LMIC.seqnoUp
  Logger::logf("TX: seqnoUp=%d; SF=%d; freq=%.1f; length=%d; uplink=0x%s", seqnoUp, sf,
               LMIC.freq / 1E6, LMIC.dataLen, txPayload.c_str());
}

void onEvent(ev_t ev) {
  // Most of the following will never happen in our use case
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Logger::log("> EV_SCAN_TIMEOUT");
      break;
    case EV_BEACON_FOUND:
      Logger::log("> EV_BEACON_FOUND");
      break;
    case EV_BEACON_MISSED:
      Logger::log("> EV_BEACON_MISSED");
      break;
    case EV_BEACON_TRACKED:
      Logger::log("> EV_BEACON_TRACKED");
      break;
    case EV_JOINING:
      Logger::log("> EV_JOINING");
      break;
    case EV_JOINED:
      Logger::log("> EV_JOINED");
      break;
    case EV_RFU1:
      // This event is defined but not triggered in the LMIC code; we could as well delete this
      Logger::log("> EV_RFU1");
      break;
    case EV_JOIN_FAILED:
      Logger::log("> EV_JOIN_FAILED");
      break;
    case EV_REJOIN_FAILED:
      Logger::log("> EV_REJOIN_FAILED");
      break;
    case EV_TXCOMPLETE:
      Logger::log("> EV_TXCOMPLETE (includes waiting for RX windows)");

      if ((LMIC.txrxFlags & TXRX_ACK) || LMIC.dataLen) {
        // Include the TX counter and SF for analysis. At this point LMIC.seqnoDn and LMIC.seqnoUp
        // have already been incremented. If the user selected data rate has changed during RX1 or
        // RX2 then this registers the wrong SF.
        String lastRxDetails = "#" + String(LMIC.seqnoDn - 1) + "/" + String(seqnoUp) + " SF" +
                               String(12 - dataRate) + " " +
                               String((LMIC.txrxFlags & TXRX_DNW1) ? "rx1" : "rx2");

        if (LMIC.txrxFlags & TXRX_ACK) {
          Logger::log("Received ACK");
          lastRxDetails += " ack";
        }

        if (LMIC.dataLen) {
          // Data received in Class A RX slot after TX
          String rxPayload = "";
          for (int i = 0; i < LMIC.dataLen; i++) {
            if (LMIC.frame[LMIC.dataBeg + i] < 0x10) {
              rxPayload += "0";
            }
            rxPayload += String(LMIC.frame[LMIC.dataBeg + i], HEX);
          }

          Logger::logf("Received %d bytes: 0x%s", LMIC.dataLen, rxPayload.c_str());
          lastRxDetails += " " + rxPayload;
        }

        display.setRxDetails(lastRxDetails);
      }

      // Schedule next transmission.
      //
      // We could try to calculate the used airtime (or change LMIC to expose its calcAirTime
      // function), and the applicable maximum duty cycle, to schedule the next transmission. But
      // when using the maximum allowed duty cycle, we can boldly tell LMIC to send right now, for
      // which LMIC will delay the actual transmission to keep within the regulations.
      //
      // Note that the maximum duty cycle is exactly that: a MAXIMUM, so using that for all
      // transmissions is NOT NICE AT ALL. Also, this does not take any TTN Fair Access Policy into
      // account. So: FOR TESTING ONLY.
      //
      // Delay a bit to allow updateStateAndDisplay to do some bookkeeping first.
      os_setTimedCallback(&sendjob, ms2osticks(500) + os_getTime(), do_send);
      break;
    case EV_LOST_TSYNC:
      Logger::log("> EV_LOST_TSYNC");
      break;
    case EV_RESET:
      Logger::log("> EV_RESET");
      break;
    case EV_RXCOMPLETE:
      // Data received in ping slot
      Logger::log("> EV_RXCOMPLETE");
      break;
    case EV_LINK_DEAD:
      Logger::log("> EV_LINK_DEAD");
      break;
    case EV_LINK_ALIVE:
      Logger::log("> EV_LINK_ALIVE");
      break;
    case EV_SCAN_FOUND:
      // This event is defined but not triggered in the LMIC code; we could as well delete this
      Logger::log("> EV_SCAN_FOUND");
      break;
    case EV_TXSTART:
      Logger::log("> EV_TXSTART");
      break;
    case EV_TXCANCELED:
      Logger::log("> EV_TXCANCELED");
      break;
    case EV_RXSTART:
      // This is actually not reported unless during debugging compliance testing, to ensure timing
      // is not mangled; see https://github.com/mcci-catena/arduino-LMIC/issues/255 Do not print
      // anything, it wrecks timing.
      break;
    case EV_JOIN_TXCOMPLETE:
      Logger::log("> EV_JOIN_TXCOMPLETE: no JoinAccept");
      break;
    default:
      Logger::logf("> Unknown event: %d", (unsigned)ev);
      break;
  }
}

int lastCountdown = 0;

/**
 * Log the seconds until the next transmission.
 */
void logTxCountdown() {
  int txSecsLeft = osticks2ms(LMIC.txend - os_getTime()) / 1000;
  if (txSecsLeft != lastCountdown) {
    lastCountdown = txSecsLeft;
    if (lastCountdown > 0) {
      Logger::printf("%d ", lastCountdown);
      if (lastCountdown % 30 == 1) {
        Logger::println();
      }
    }
  }
}

// Endless loop that does not return
[[noreturn]] void stateAndDisplayTask(__unused void *pvParameters) {
  Logger::logf("Task stateAndDisplayTask running on core %d", xPortGetCoreID());

  // As this task runs in a different core, we also need to initialize in that same core, as that
  // allocates a buffer for the display.
  display.init();

  // We only need to display 1/10th of a second, but for a smooth progress bar we need a bit more
  const TickType_t xDelay = 50 / portTICK_PERIOD_MS;

  while (true) {
    // To keep logging of updateStateAndDisplay and logTxCountdown in sync, invoke from same core
    updateStateAndDisplay();
    display.tick();
    logTxCountdown();
    vTaskDelay(xDelay);
  }
}

TaskHandle_t stateAndDisplayTaskHandle;

void setupStateAndDisplayTask() {
  // The task running setup() and loop() is created on core 1 with priority 1
  Logger::logf("Main loop running on core %d", xPortGetCoreID());

  // The stack size is trial and error, and includes the buffers for the display and log formatter
  xTaskCreatePinnedToCore(stateAndDisplayTask, "DisplayTask",
                          2600, // Stack size in words
                          nullptr, // Parameters for the task
                          1, // Priority of the task
                          &stateAndDisplayTaskHandle,
                          1 - xPortGetCoreID()); // Core for the task
}

OneButton stateButton; // NOLINT(cert-err58-cpp)

void setupStateButton() {
  // Button is active LOW
  stateButton = OneButton(STATE_BUTTON, true);
  stateButton.attachClick(nextDataRate);
  stateButton.attachDoubleClick(toggleConfirmed);
  stateButton.attachLongPressStart(toggleAutoDataRate);
}

const lmic_pinmap lmic_pins = LMIC_PINS;

// These callbacks are only used for OTAA, so they are left empty here. (We cannot leave them out
// completely unless `-D DISABLE_JOIN` is added to the build flags, but then the compiler still
// throws warnings about unused code and implicit definitions.)
void os_getArtEui(__unused u1_t *buf) {}
void os_getDevEui(__unused u1_t *buf) {}
void os_getDevKey(__unused u1_t *buf) {}

void setupLMIC() {
  os_init();

  // Reset the MAC state; session and pending data transfers will be discarded
  LMIC_reset();

  // ABP: set static session parameters
#ifdef PROGMEM
  // On AVR, these values are stored in flash and only copied to RAM once. Copy them to a temporary
  // buffer here, LMIC_setSession will copy them into a buffer of its own again.
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x13, DEVADDR, nwkskey, appskey);
#else
  // If not running an AVR with PROGMEM, just use the arrays directly
  LMIC_setSession(0x13, DEVADDR, NWKSKEY, APPSKEY);
#endif

  // Set up the EU868 channels used by TTN. Without this, only three base channels from the
  // LoRaWAN specification are used, which certainly works, so it is good for debugging, but can
  // overload those frequencies, so be sure to configure the full frequency range of your network
  // here (unless your network auto-configures them). Setting up channels should happen after
  // LMIC_setSession, as that configures the minimal channel set. LMIC doesn't let you change the
  // three basic settings, so these are just included for documentation here.

  // g-band:
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
  // g2-band:
  LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK, DR_FSK), BAND_MILLI);

  // TTN defines an additional channel at 869.525Mhz using SF9 for class B devices' ping slots. LMIC
  // does not have an easy way to define this frequency and Class B is not even used here, so this
  // frequency is not configured here.

  // TTN uses SF9 for its EU868 RX2 window
  LMIC.dn2Dr = DR_SF9;

  // Disable Adaptive Data Rate; enabling makes no sense, given we want to cycle different SFs
  LMIC_setAdrMode(0);

  // Disable link check validation
  LMIC_setLinkCheckMode(0);

  // Set data rate and transmit power (for this sketch, this is not needed as it's repeated before
  // each uplink)
  LMIC_setDrTxpow(SF7, 14);

  // Beware that RX1 may fail while RX2 works fine. Like for the Heltec WiFi LoRa 32 board used for
  // testing, RX2 in EU868 (using SF9) seems to work with the standard settings, but RX1 for SF8
  // needs 2%, while RX1 for SF7 even needs 5% of the maximum error.
  //
  // For corrections larger than 0.4% this needs `LMIC_ENABLE_arbitrary_clock_error`; see
  // https://github.com/mcci-catena/arduino-LMIC/blob/master/README.md#LMIC_setclockerror
  LMIC_setClockError((MAX_CLOCK_ERROR * 5) / 100);
}

void setup() {
  // Increase the chance we see the first lines of logging after uploading new code
  delay(200);
  Logger::log("Starting data-rate-tester");

  setupStateAndDisplayTask();
  setupStateButton();
  setupLMIC();

  do_send(&sendjob);
}

void loop() {
  os_runloop_once();
  stateButton.tick();
}
