# Power usage: Waveshare ESP32-S3-RLCD-4.2

This is a simple overview of what can drain the battery on the Waveshare ESP32-S3-RLCD-4.2 and how each part does it.

The important distinction is between **energy used briefly while the device is awake** and **current used continuously while it is asleep**. The second category usually matters much more for battery life.

## Biggest possible drain: not reaching deep sleep

When the firmware is working normally, the ESP32 wakes up, updates the screen, and returns to deep sleep. If it does not reach deep sleep, the processor and its peripherals remain active and the battery can be empty in a few days.

The firmware checks `usb_serial_jtag_is_connected()` before sleeping. A false USB detection would keep the device awake. Floating USB data lines were therefore a credible explanation for severe battery drain, although this needs to be verified by measuring current on the real board.

Wi-Fi is another large short-term drain. It is enabled only when the network screen is requested, then disconnected and switched off. A failed connection can still keep the ESP32 awake for up to the configured timeout.

## Continuous hardware drain during sleep

Even in deep sleep, the board's power circuitry and some connected components can draw current:

- **Power converters:** The ETA6098 and TPS63020 keep the board's supply rails running. Their quiescent current and conversion losses are paid continuously, even when the ESP32 sleeps.
- **Audio power rail:** The schematic shows an RT9193 regulator supplying the audio circuitry. Its idle current is present if that rail cannot be disabled.
- **Battery voltage divider:** The battery measurement resistors are permanently connected across the cell. With approximately 300 kOhm total resistance, they draw roughly 14 microamps continuously. This is small, but it never stops.
- **Display:** The RLCD does not have a backlight, which saves a lot of power. However, it needs its supply to preserve the displayed image. The firmware holds the display control pins during deep sleep so the image remains visible.
- **PSRAM and flash:** The ESP32-S3-WROOM-1-N16R8 includes 8 MB PSRAM and 16 MB flash. PSRAM may add sleep current depending on its power mode and configuration. This project does not otherwise need PSRAM, so a measured comparison with PSRAM disabled could be useful.

These hardware loads cannot all be removed in software. The only reliable way to find their combined value is to measure the battery current while the board is demonstrably in deep sleep.

## Audio hardware

The board contains:

- ES8311 audio codec
- ES7210 microphone ADC
- two microphones
- NS4150B speaker amplifier

The ES8311 and ES7210 datasheets indicate that their analogue circuits, ADCs, microphone bias, and DAC are powered down by their reset defaults. The current firmware never initializes them, so they are not expected to be actively recording or playing audio.

The speaker amplifier is different. Its enable/control input is connected to **GPIO46** (`PA_CTRL`). If that pin floated high, the amplifier could remain enabled and become a meaningful continuous drain. The firmware therefore drives GPIO46 low at startup and holds it low through deep sleep (`hardwareBegin()` / `deepSleep()` in `hardware.cpp`). Verify the effect with a current measurement.

This is a risk to test, not a confirmed drain: the available documentation did not establish whether the amplifier has an internal pull-down.

## Other onboard parts

- **SHTC3 temperature/humidity sensor:** It is only read once per minute by this project. Its contribution is brief and small compared with Wi-Fi or a device that fails to sleep.
- **PCF85063 RTC:** It is designed for low-power timekeeping and is needed to retain the clock through deep sleep. It should not be a major drain.
- **TF card slot:** No card is mounted or accessed by this firmware. The empty slot should not be a significant load.
- **Charging and battery protection:** The ETA6098 charger/boost circuit and S8261 protection circuit are hardware loads. Charging indicators can also consume current when their hardware conditions are met, but they are not controlled by the firmware.
- **Status LEDs:** The board has charge and reverse-battery warning indicators. The warning LED should not be lit with a correctly inserted battery. The charging LED may be lit while charging.

## What the current firmware spends power on

During a normal local-screen update, the ESP32:

1. wakes from deep sleep;
2. reads the SHTC3 and battery ADC;
3. redraws the 400x300 RLCD over SPI;
4. returns to deep sleep.

This active work happens only briefly once per minute. Moving the refresh interval to five minutes would reduce this active cost, but it is unlikely to solve a battery that empties in two or three days unless the active period is unexpectedly long or the device is failing to sleep.

When the KEY button is pressed, the firmware additionally enables Wi-Fi, fetches the server data, updates the display, and turns Wi-Fi off again. This is the most expensive intentional operation, but it happens only on demand.

## Practical conclusions

1. **First verify that deep sleep is really reached.** This is the highest-value check. Measure battery current after the display update has finished and the board has entered its sleep path.
2. **Test GPIO46.** Explicitly disabling and holding the speaker amplifier is the most plausible additional firmware power fix found in the schematic review.
3. **Do not focus on the SHTC3 first.** Reading temperature and humidity once per minute is unlikely to explain multi-day battery life.
4. **Compare PSRAM configurations only after measuring baseline current.** It may help, but changing memory settings without a current measurement gives no useful proof.
5. **Treat battery percentage separately from battery drain.** The voltage calibration changes what the displayed percentage means; it does not make the battery last longer.

## Sources

- [Waveshare ESP32-S3-RLCD-4.2 documentation](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
- [Waveshare ESP32-S3-RLCD-4.2 schematic](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf)
- [Waveshare example repository](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
- [ES8311 datasheet](https://files.waveshare.com/wiki/common/ES8311.DS.pdf)
- [ES7210 datasheet](https://files.waveshare.com/wiki/common/ES7210_DS.pdf)
