#ifndef DATA_RATE_TESTER_DISPLAY_H
#define DATA_RATE_TESTER_DISPLAY_H

#include <Wire.h>
#include "SSD1306Wire.h"

enum State { STATE_WAITING, STATE_TX, STATE_RX1, STATE_RX2, STATE_RXDONE, STATE_NOP };

class Display {

private:
  SSD1306Wire oled;

  bool isConfirmedUplink{false};
  bool isFixedDataRate{false};
  String lastRxDetails;
  uint32_t fcnt{0};
  uint32_t freq{0};
  uint8_t sf{7};

  State state{STATE_NOP};
  uint32_t progressStartTime{0};
  uint32_t progressTargetTime{0};

  void startWait(State state, uint32_t targetTimeMs);
  void showSplash();

public:
  Display();

  void init();
  void tick();

  void setIsConfirmedUplink(bool isConfirmed);
  void setIsFixedDataRate(bool isFixed);
  void setRxDetails(const String &rxDetails);
  void setTxCount(uint32_t fCntUp);
  void setTxFreq(uint32_t txFreq);
  void setTxSpreadingFactor(uint8_t spreadingFactor);

  void startWaitTx(uint32_t targetTimeMs);
  void startTx();
  void startWaitRx1(uint32_t targetTimeMs);
  void startWaitRx2(uint32_t targetTimeMs);
  void stop();
};

extern Display display;

#endif // DATA_RATE_TESTER_DISPLAY_H
