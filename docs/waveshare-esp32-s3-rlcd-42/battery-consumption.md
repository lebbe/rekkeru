# Waveshare ESP32-S3-RLCD-4.2: battery consumption findings

Closes lebbe/rekkeru#1. This is a write-up of what was investigated for that issue, followed
by a small set of incremental firmware patches. Nothing here has been verified with a
current-clamp/coulomb-counter on real hardware — it's based on reading the firmware, the
ESP-IDF/Arduino deep-sleep documentation, and Waveshare's own example code for this board
(`waveshareteam/ESP32-S3-RLCD-4.2` on GitHub, in particular `02_Example/ESPHome` and
`02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/adc_bsp.cpp`). Treat the patches as
things to try and measure, not as a guaranteed fix — hence one commit per patch, as requested.

## How the device currently spends power

`main.cpp` deep-sleeps between updates and only wakes on the RTC timer or the KEY button
(`deepSleep()` in `hardware.cpp`). In the common case (local clock screen) it wakes once a
minute, reads the SHTC3 climate sensor and the battery ADC, redraws the whole 400×300 display
over SPI, and goes back to deep sleep. Rough back-of-envelope math on the *active* part of that
cycle (SHTC3 read ≈ 16 ms, full 400×300 1-bpp screen redraw at 1 MHz SPI ≈ 120 ms, Wi-Fi off)
puts the awake energy per wake at roughly 0.01 mAh, i.e. on the order of 14 mAh/day at
1440 wakes/day. That is still nowhere near enough to explain a battery that is flat after 2–3
days on an 18650 (2000+ mAh) — at that rate the battery would take months to drain — so the
*sleep* current, or a failure to actually reach deep sleep, matters far more than the per-wake
work:

- **Waking up every minute is cheap by itself.** Reducing the redraw interval to every five
  minutes (as suggested in the issue) would cut the number of wakes ~5×, but since each wake
  is already a tiny fraction of the 60 s budget, the expected saving is small compared to the
  baseline deep-sleep draw. It's still worth trying on real hardware since it's a one-line
  change and any saving is free, but it should not be expected to fix a 2–3 day battery life on
  its own.
- **`usbConnected()` gates whether the board deep-sleeps at all.** When it returns `true`,
  `loop()` never calls `deepSleep()` and instead busy-waits/redraws in a loop — this is
  intentional while developing over USB, but if `usb_serial_jtag_is_connected()` ever reports a
  false positive while running on battery only (e.g. picking up electrical noise on the D+/D-
  lines, which float when nothing is attached), the board would never enter deep sleep and would
  run its CPU/peripherals continuously, which could easily flatten a battery in a day or two.
  This was hardened defensively (see patch below), since it's cheap insurance against the
  single largest possible drain and can't be ruled out without instrumenting a real board.
- **GPIO hold across deep sleep.** `screenSleep()` holds the display CS/RST pins (GPIO40/41)
  so the ST7305's low-power-mode image survives while the ESP32 sleeps, and enables global
  deep-sleep pad hold with `gpio_deep_sleep_hold_en()`. These are digital GPIOs, not ESP32-S3 RTC
  GPIOs, so the RTC-peripheral-domain caveat for RTC GPIO hold does not directly apply. Retaining
  the pads is deliberate to avoid a full display re-initialization flash; measure its sleep-current
  impact on hardware before treating it as a contributor.
- **Sensors read every wake regardless of which screen is shown.** `wake()` unconditionally
  read the SHTC3 and the battery ADC even on wakes that only refresh the network/departures
  screen, which never displays either value. This wastes a small but avoidable amount of I²C
  traffic and SHTC3 duty cycling; fixed below.

## Battery percentage accuracy

`batteryPercent()` linearly maps the measured voltage between `BATTERY_EMPTY_VOLTS` (2.5 V) and
`BATTERY_FULL_VOLTS` (4.2 V) to 0–100 %. Two things line up with the reported symptoms:

1. **The empty threshold was too low.** A single-cell Li-ion/LiPo cell (as used with the
   18650 holder on this board) is not usable down to 2.5 V — most protection circuits and fuel
   gauges treat somewhere around 3.0–3.3 V (no load) as empty, and the regulator on the board
   will brown out long before 2.5 V is reached in practice. With the old calibration, a real
   "the board is refusing to run any more" voltage of ~3.0 V is reported as `(3.0 - 2.5) /
   (4.2 - 2.5) * 100 ≈ 29 %` — which matches "it starts around 28 % when it is empty" almost
   exactly. Moving the empty threshold up to a more realistic 3.3 V makes the percentage track
   the cell's actual usable range instead of showing "28 % remaining" once the board is already
   unusable.
2. **100 % (and 0 %) is a moving target through the divider/ADC tolerances.** A resting,
   fully-charged cell rarely sits at exactly 4.20 V (charge termination detection, small drops
   across the divider, and ADC calibration all shave off a little), so a strict linear mapping
   will almost never show exactly 100 % even when the battery is, for all practical purposes,
   full. The fix applied here snaps percentages very close to either end to 0/100, which is
   standard practice in fuel-gauge UIs and avoids a battery that reads "99 %" forever.
3. **A truly accurate percentage would need a non-linear (LiPo discharge-curve) mapping**, since
   cell voltage sits around 3.7–3.9 V for most of the discharge curve and only drops steeply near
   empty. That's a bigger change with more parameters to get wrong without being able to verify
   against a real discharge curve for the exact cell in this device, so it was left out of this
   pass; the linear mapping with corrected end points is a much smaller, safer change that
   addresses the two concretely reported symptoms.

Confirmed against Waveshare's own examples for this board: the ESPHome example
(`02_Example/ESPHome/examples/esp32-s3-rlcd-42-sensor.yaml`) uses the exact same
`analogReadMilliVolts()` × 3 divider ratio and the same 2.5 V/4.2 V calibration this firmware
started with, while the factory ESP-IDF firmware
(`02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/adc_bsp.cpp`) uses a narrower
3.0 V–4.12 V range — supporting the idea that 2.5 V as "empty" is unrealistically low for this
hardware.

## Patches in this PR

Each of these is its own commit so they can be tested independently:

1. Recalibrate `BATTERY_EMPTY_VOLTS` to 3.3 V and snap percentages near the limits to 0/100.
2. Only read the climate/battery sensors when the local screen is about to be drawn, instead of
   on every wake.
3. Require `usb_serial_jtag_is_connected()` to report "connected" consistently over the sampling
   window before trusting it, instead of latching onto the first positive reading.

## Ideas not implemented here (need hardware to verify)

- Measure actual deep-sleep current draw with a multimeter/USB power meter in series with the
  battery, to find out whether the board really reaches the expected low-µA deep sleep, or
  whether it's staying awake (confirms/refutes the `usbConnected()` hypothesis above).
- Try a longer local-screen refresh interval (e.g. 5 minutes) on real hardware and compare
  measured battery life, now that the "cheap win" patches above are in place.
- If the battery still reads inaccurately after the recalibration, capture a real discharge
  curve for the cell in use and switch `batteryPercent()` to a small lookup table instead of a
  straight line.
