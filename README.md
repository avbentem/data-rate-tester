# LoRaWAN EU868 Data Rate Tester

Test LoRaWAN uplinks by quickly cycling through different data rates, (ab)using the EU868 maximum
duty cycle, optionally using confirmed uplinks to also test downlinks (but without actually retrying
if no confirmation is received), and always using the maximum transmission power.

[![Heltec board in Tic Tac box](./doc/device.png)](./doc/device.png)

:warning: Regional maximum duty cycle regulations may not be the only limitation that applies. For
The Things Network, 30 seconds uplink and 10 downlinks per day apply for its Fair Access Policy.

:warning: The code to display the timers and to disable the auto-retry for confirmed uplinks is
mostly trial & error and very much relies on internals of a specific LMIC version. It is not a good
base for further development.

## Usage

While awaiting a transmission, the display shows the uplink counter, SF and channel for the upcoming
uplink, and shows the same while awaiting its receive windows. For a downlink it also shows the
uplink's counter and SF, along with the downlink counter and receive window. As RX1 in EU868 uses
the same settings as the uplink, and RX2 in EU868 always uses SF9, the downlink's SF is not
explicitly displayed.

A single button is used for control:

- Press once to skip to the next data rate.

- Press twice to toggle confirmed/unconfirmed uplinks. An asterisk after the data rate indicates
  that confirmed uplinks are enabled.

- Long press to toggle between automatic cycling through the predefined list of data rates, and
  manual cycling through SF7..SF12. Square brackets around the data rate indicate that it is fixed.
  (Changing this mode after a transmission, while awaiting the receive windows for a confirmed
  uplink, may yield the wrong uplink details to be displayed with the downlink.)

The predefined list cycles through SF7, SF8, SF9, SF7, SF12, SF7, SF8, SF10, SF8, SF9, SF11, SF7.
This order prioritizes testing the better data rates, while balancing the waiting time between
uplinks, and while still allowing for quickly switching to manual mode after starting:

         Airtime (hence duty cycle waiting time)
     SF7 ∎
     SF8 ∎∎
     SF9 ∎∎∎∎
     SF7 ∎
    SF12 ∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎
     SF7 ∎
     SF8 ∎∎
    SF10 ∎∎∎∎∎∎∎∎
     SF8 ∎∎
     SF9 ∎∎∎∎
    SF11 ∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎∎
     SF7 ∎

This does not test DR6 (SF7BW250) nor FSK, which will both be short range anyhow.

[The photo](./doc/device.png) further above above shows:

- `#20 [SF8]* 867.1`
  - `#20` - next uplink will use frame counter 20.
  - `SF8` - next uplink will use data rate SF8BW125.
  - `[..]` - manual mode is active; subsequent uplinks will also use SF8BW125.
  - `*` - confirmed uplinks are active.
  - `867.1` - next uplink will use 867.1 MHz.

- `tx in 4.9 sec` - the countdown progress bar, showing how much waiting time is left, to only
  comply with (or: to _abuse_) the maximum duty cycle regulations.

- `#26/19 SF8 rx1 ack`
  - `#26/19` - the last downlink counter was 26, and was received after uplink 19. The downlink
    counter being larger than the uplink counter implies that the device was restarted without
    resetting the counters in TTN Console, and also implies that apparently the frame counter
    security was disabled in TTN Console.
  - `SF8` - uplink 19 was sent using SF8BW125.
  - `rx1` - downlink 26 was received in RX1 (hence for EU868 was using the same SF as the uplink; if
    it was received in RX2 then for EU868 it would have used SF9BW125).
  - `ack` - the downlink had its FCtrl.ACK bit set.

For an actual application-level downlink, the downlink data will be shown at the bottom as well.

## Setup

This has only been tested with [the first release](doc/heltec-wifi-lora-32/README.md) of the "Heltec
WiFi LoRa 32" board, for EU868, on [PlatformIO][pio] 4.3.4.

[pio]: https://platformio.org/

- Connect a button to `GND` and `IO0` (or use the on-board program button).

- Register an ABP device. For TTN Console V3 you will need to be explicit 
  [about channels preset in the device][preset]. This depends on your hardware and has been tested
  with the settings below. You probably also want to disable frame counter security.

  [preset]: https://www.thethingsnetwork.org/forum/t/why-is-my-abp-device-not-seen-in-v3/53300/10

  In TTN Console V3, in the device's General settings, Network layer:

  - Frequency plan: _Europe 863-870 MHz (SF9 for RX2 - recommended)_
  - LoRaWAN version: _MAC V1.0.2_
  - Regional Parameters version: _PHY V1.0.2 REV B_
  - Advanced MAC settings:
    - Frame counter width: _32 bits_ (default)
    - RX1 Delay: _1 second_ (default)
    - RX1 Data Rate Offset: _0_ (default)
    - Reset Frame Counters: _enabled_ (at least for _MAC V1.0.2_ along with _PHY V1.0.2 REV B_
      this [also resets the downlink counter][downlink] as used by TTN, when it detects that the
      uplink counter was reset)
    - RX2 Data Rate Index: _3_ (default)
    - RX2 Frequency: _869525000_ (default)
    - Factory Preset Frequencies: _868100000_, _868300000_, _868500000_, _867100000_, _867300000_,
      _867500000_, _867700000_, _867900000_

    [downlink]: https://www.thethingsnetwork.org/forum/t/how-to-reset-frame-counter-on-v3/47884/6

- Copy [`include/config-example.h`](include/config-example.h) into a new file `config.h` and
  configure the LoRaWAN ABP settings.

- When not using EU868, or when not using The Things Network:

  - In [`platformio.ini`](platformio.ini) set [the MCCI LMIC build flags][mcci_flags]. 

    [mcci_flags]: https://github.com/mcci-catena/arduino-lmic#platformio
  
  - In [`main.cpp`](src/main.cpp) review all code labeled `EU868`, and all code using
    `12 - LMIC.datarate`, like calls to `LMIC_setupChannel` and `display.setSpreadingFactor`.

- Execute `pio run` to create the hidden `.pio` folder, download dependencies, build the project,
  and upload it to the board (if connected).

## Implementation choices

- `LinkCheckReq` may be a more descriptive alternative for a confirmed uplink, but MCCI LMIC 3.2.0
  [does not seem to support LinkCheckAns][LinkCheckAns].
  
  [LinkCheckAns]: https://github.com/mcci-catena/arduino-lmic/blob/v3.2.0/src/lmic/lmic.c#L917-L921
 
- The state machine and display handling is running in its own core; of course that's quite some
  overkill for this simple use case. In fact, it may mess up logging a bit, when both cores write to
  the log simultaneously.
  
## Common issues

### error: 'DR_SF12' was not declared in this scope

You did not configure MCCI LMIC to use EU868.
