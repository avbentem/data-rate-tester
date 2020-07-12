/**
 * Controls the OLED display, showing the next uplink details, a progress bar indicating when the
 * next event happens, and the last downlink details if applicable.
 *
 * See https://github.com/ThingPulse/esp8266-oled-ssd1306
 */
#include "display.h"
#include "config.h"
#include "fonts.h"
#include "images.h"
#include "logger.h"

// Global singleton instance
Display display; // NOLINT(cert-err58-cpp)

Display::Display() : oled(OLED_ADDRESS, SDA_OLED, SCL_OLED) {}

void Display::showSplash() {
  oled.clear();
  oled.drawXbm((128 - logo_width) / 2, (64 - logo_height) / 2, logo_width, logo_height, logo_bits);
  oled.display();
  // Only show very briefly, as meanwhile the LoRaWAN TX will already be running in the other core.
  delay(200);
}

void Display::init() {
  // We cannot run all this in the constructor, when using a global instance that is created long
  // before all dependencies have been initialized
  pinMode(RST_OLED, OUTPUT);
  // Set reset pin GPIO16 low to reset OLED
  digitalWrite(RST_OLED, LOW);
  delay(50);
  // Set reset pin GPIO16 high while OLED is running
  digitalWrite(RST_OLED, HIGH);

  oled.init();
  oled.flipScreenVertically();
  // Try to avoid burn-in of details such as the progress bar
  oled.setBrightness(80);
  showSplash();
}

void Display::tick() {
  // The range may be negative for state changes that did not define new values, like during TX.
  int32_t rangeMs = progressTargetTime - progressStartTime;

  // The time left will also become negative at the end of STATE_RX1, while actually listening. This
  // is especially true when using large values for the LMIC clock error, for which the RX1 wait
  // time is much less than 1 second, and for which the RX2 wait time also starts later as the RX1
  // window is longer. As a result of the negative values, one sees a smooth progress bar. (Also,
  // millis will overflow after 40 days, which is not an issue here.)
  int32_t timeLeftMs = progressTargetTime - millis();

  float sec = timeLeftMs / 1000.0f;

  String label;
  uint8_t progress = 0;

  switch (state) {
    case STATE_WAITING:
      // This may become slightly negative; suppress
      if (sec >= 0) {
        label = "tx in " + String(sec, 1) + " sec";
        progress = rangeMs > 0 ? min(int32_t(100 - (100 * timeLeftMs / rangeMs)), 100) : 0;
      } else {
        progress = 100;
      }
      break;

    case STATE_TX:
      // Though we expect LMIC to be transmitting, it may actually apply some safety zone before
      // it's doing that. That's okay.
      label = "tx " + (sec < 0.1 ? String(-sec, 1) + " sec" : "");
      // label = "tx";
      progress = 100;
      break;

    case STATE_RX1:
      if (sec > 1) {
        label = "unknown rx1 state " + String(sec, 1) + " sec";
      } else {
        if (sec <= 0.1) {
          // See comment about negative values above
          label = "rx1 " + (sec < -0.5 ? String(sec, 1) + " sec" : "");
        } else {
          // label = "rx1 in " + String(sec, 1) + " sec";
          label = "awaiting rx1";
        }
        // 1.0..0 reading as 100..50 (rightmost half of the backwards progress bar)
        progress = 50 + int(50 * sec);
      }
      break;

    case STATE_RX2:
      if (sec > 1) {
        label = "unknown rx2 state: " + String(sec, 1) + " sec";
      } else {
        if (sec <= 0.1) {
          label = "rx2 " + (sec < -0.5 ? String(sec, 1) + " sec" : "");
        } else {
          // label = "rx2 in " + String(sec, 1) + " sec";
          label = "awaiting rx2";
        }
        // 1.0..0 reading as 50..0 (leftmost half of the backwards progress bar)
        progress = max(int32_t(50 * sec), 0);
      }
      break;

    case STATE_NOP:
      label = "";
      break;

    default:
      label = "unknown state " + String(sec, 1) + " sec";
  }

  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);

  // Available default fonts: ArialMT_Plain_10, ArialMT_Plain_16, ArialMT_Plain_24. Or create one
  // with the font tool at http://oleddisplay.squix.ch
  oled.setFont(Open_Sans_Condensed_Light_18);
  oled.drawString(64, 0,
                  "#" + String(fcnt) + (isFixedDataRate ? " [" : " ") + "SF" + String(sf) +
                      (isFixedDataRate ? "]" : "") + (isConfirmedUplink ? "* " : " ") +
                      String(freq / 1E6, 1));
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 24, label);
  oled.drawProgressBar(0, 40, 127, 6, progress);
  oled.drawString(64, 52, lastRxDetails);
  oled.display();
}

void Display::setIsConfirmedUplink(const bool isConfirmed) {
  isConfirmedUplink = isConfirmed;
}

void Display::setIsFixedDataRate(const bool isFixed) {
  isFixedDataRate = isFixed;
}

void Display::setRxDetails(const String &rxDetails) {
  lastRxDetails = rxDetails;
}

void Display::setTxCount(const uint32_t fCntUp) {
  fcnt = fCntUp;
}

void Display::setTxSpreadingFactor(const uint8_t spreadingFactor) {
  sf = spreadingFactor;
}

void Display::setTxFreq(const uint32_t txFreq) {
  freq = txFreq;
}

void Display::startWait(const State waitState, const uint32_t targetTimeMs) {
  state = waitState;
  progressStartTime = millis();
  progressTargetTime = targetTimeMs;
  Logger::logf("Start progress bar: state=%d; time=%d ms", waitState,
               progressTargetTime - progressStartTime);
}

void Display::startWaitTx(const uint32_t targetTimeMs) {
  startWait(STATE_WAITING, targetTimeMs);
}

void Display::startTx() {
  state = STATE_TX;
}

void Display::startWaitRx1(const uint32_t targetTimeMs) {
  startWait(STATE_RX1, targetTimeMs);
}

void Display::startWaitRx2(const uint32_t targetTimeMs) {
  startWait(STATE_RX2, targetTimeMs);
}

void Display::stop() {
  state = STATE_NOP;
}
