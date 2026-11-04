# ESP32 Xbox → CRSF Bridge

**Use an Xbox Wireless controller as an RC transmitter for ArduPilot flight controllers, with no ExpressLRS radio pair required.**

An ESP32 pairs with the controller over Bluetooth LE and transmits standard **CRSF** (ExpressLRS-compatible) RC frames on a UART. The flight controller sees an ordinary RC receiver, so Mission Planner, arming, flight modes and ESC outputs all behave as they would with a real radio.

> **Range note:** Bluetooth LE gives roughly 10 m and 10-30 ms of latency. This is built for bench rigs, ESC and motor testing, labs, demos and teaching. It is not a long-range ELRS replacement.

---

## Features

- 16-channel CRSF `RC_CHANNELS_PACKED` frames at **100 Hz**, 416666 baud, 11-bit resolution, CRC-8 (poly `0xD5`)
- Pairs directly with an Xbox Wireless controller over BLE (no PC or dongle)
- Channel order matches ArduPilot's default `RCMAP` (Roll, Pitch, Throttle, Yaw)
- **Spring-return throttle** (stick up or right trigger), so releasing the control means zero throttle
- **Live throttle limiter** on the D-pad (10%-100%, starts at 30%) for safer bench testing
- **Emergency stop** on B (throttle to zero and disarm)
- **Fail-safe by silence:** if the controller disconnects, the ESP32 stops transmitting and the flight controller raises its RC failsafe
- Built-in serial diagnostics: 1 Hz status line, frame counters, loopback self-test, FC telemetry sniffing

## How it works

```
 Xbox controller ──BLE──► ESP32 (WROOM-32) ──UART, CRSF 416666 8N1──► Flight controller ──► ESC / motors
                          (this firmware)                             (ArduPilot, RCIN)
```

Each 10 ms the firmware reads the sticks, triggers and buttons, converts them to microsecond values (1000-2000), maps those to CRSF ticks (172-1811, centre 992), packs 16 channels × 11 bits into a 22-byte payload, adds the header and CRC-8, and writes the 26-byte frame to `Serial2`.

---

## Hardware

### Bill of materials

| Item | Notes |
|---|---|
| ESP32-WROOM-32 dev board | Use **WROOM**, not WROVER (WROVER uses GPIO16/17 for PSRAM) |
| Xbox Wireless controller | Must support **Bluetooth LE**: Xbox Series X\|S or newer Xbox One S models. Old Xbox One controllers without Bluetooth will not work |
| ArduPilot flight controller | Any board with a spare UART that can be set to RC input |
| 5 V supply for the ESP32 | A small BEC or regulator. Do not rely on a flight controller serial-port 5 V pin, as Bluetooth peaks at ~250 mA |
| Jumper wires | |

### Wiring

| ESP32 | Flight controller | Required? |
|---|---|---|
| GPIO17 (UART2 TX) | RX of the UART set to RC input | **Yes** |
| GPIO16 (UART2 RX) | TX of the same UART | Optional (telemetry sniffing, loopback test) |
| GND | GND | **Yes** |
| VIN / 5V | 5 V BEC | **Yes** |

Both sides use 3.3 V logic. The pins can be changed with `CRSF_TX_PIN` and `CRSF_RX_PIN` in `src/main.cpp`.

### Flight controller setup (ArduPilot)

Set these in Mission Planner (**Config → Full Parameter List**) and write them. Reboot the board afterwards.

| Parameter | Value | Purpose |
|---|---|---|
| `SERIALn_PROTOCOL` | `23` (RCIN) | `n` is the port wired to the ESP32. The baud field is ignored for RC input |
| `RC_PROTOCOLS` | default (All) | Or make sure the CRSF bit is enabled |
| `RC6_OPTION` | `153` (Arm/Disarm) | Makes the **A** button arm |
| `FLTMODE_CH` | `5` (default) | Flight modes on channel 5 |

Then calibrate under **Setup → Mandatory Hardware → Radio Calibration** (set the throttle limiter to 100% while doing this, then bring it back down).

**Bench only:** on a bare board the pre-arm checks will block arming. For motor tests with **propellers removed**, you can skip them with `ARMING_SKIPCHK = -1` (newer ArduPilot) or `ARMING_CHECK = 0` (older versions). Restore the defaults before any flight.

### Compatibility

| Flight controller / firmware | Status |
|---|---|
| Custom STM32H743 board running ArduPilot Copter, Mission Planner 1.3.83 | Channels verified in Radio Calibration and the Status tab |
| Pixhawk 2.4.8 (ArduPilot) | Expected to work with the same settings, **not yet tested** |
| Betaflight / iNav | Not tested. Would need the CRSF serial-RX setting on the FC |

---

## Software setup

1. **Install [VS Code](https://code.visualstudio.com/).**
2. **Install the PlatformIO IDE extension.** If you also have the *pioarduino IDE* extension, VS Code asks you to keep only one active. This repo's `platformio.ini` targets the official `espressif32` platform, so keep **PlatformIO IDE**. If you pin a pioarduino platform instead, keep pioarduino IDE.
3. **Install the USB-UART driver** for your board. Check which chip sits next to the USB port: **CP210x** (Silicon Labs) or **CH340/CH343** (WCH). Windows 10/11 often installs these automatically. If no COM port appears in Device Manager, install the vendor driver.
4. **Clone and open:**
   ```bash
   git clone https://github.com/<your-username>/esp32-xbox-crsf-bridge.git
   ```
   Open the folder in VS Code. PlatformIO downloads the toolchain and the libraries on first build.
5. **Build and upload** with the PlatformIO toolbar (check mark = build, arrow = upload). If upload stalls at "Connecting...", hold the board's **BOOT** button until it starts.
6. **Open the serial monitor** at **115200** baud.
7. **Pair the controller:** hold the Xbox **Sync** button (top edge) until the logo flashes quickly. The monitor prints `[EVT] Xbox CONNECTED`. Make sure the controller is not already linked to a PC or console.

Libraries (pulled in automatically): [XboxSeriesXControllerESP32_asukiaaa](https://github.com/asukiaaa/XboxSeriesXControllerESP32_asukiaaa) and [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino).

### Tested with

| Component | Version |
|---|---|
| PlatformIO platform `espressif32` | _fill in_ (`pio pkg list`) |
| Arduino-ESP32 core | _fill in_ |
| XboxSeriesXControllerESP32_asukiaaa | _fill in_ |
| NimBLE-Arduino | _fill in_ |
| Controller model | _fill in_ |
| ArduPilot firmware | _fill in_ |

---

## Controls

| Input | Function |
|---|---|
| Left stick **up** or right trigger **RT** | Throttle (CH3), 0 when released |
| Left stick X | Yaw (CH4) |
| Right stick X / Y | Roll (CH1) / Pitch (CH2) |
| **A** | Arm / disarm toggle (CH6) |
| **B** | Emergency stop: throttle 0 and disarm |
| **LB / RB** | Flight mode down / up (CH5, six mode slots) |
| **D-pad Up / Down** | Throttle limiter +10% / −10% (starts at 30%) |
| **X**, **Y**, **D-pad Left**, **D-pad Right** (hold) | Aux CH7, CH8, CH9, CH10 go high (2000) while held |

### Channel map

| CH | Function | Range (µs) |
|---|---|---|
| 1 | Roll | 1000-2000, centre 1500 |
| 2 | Pitch (stick forward = low) | 1000-2000, centre 1500 |
| 3 | Throttle | 1000 to 1000 + 1000 × limiter |
| 4 | Yaw | 1000-2000, centre 1500 |
| 5 | Flight mode | 1100 / 1295 / 1425 / 1555 / 1685 / 1900 |
| 6 | Arm | 1100 (disarm) / 1900 (arm) |
| 7-10 | Aux (X, Y, D-pad L, D-pad R) | 1000 (released) / 2000 (held) |
| 11-16 | Unused | 1500 |

Use `RCx_OPTION` parameters to attach any ArduPilot auxiliary function to CH7-CH10.

### Throttle limiter

The limiter scales the stick: with a 30% cap, full stick sends 1300 µs. Each D-pad press changes it by 10%, with no repeat when held. It resets to 30% when the ESP32 restarts. ArduPilot decides how that RC value maps to motor output (`MOT_SPIN_ARM`, `MOT_SPIN_MIN`, thrust curve), so the cap limits the RC input and is not a guaranteed motor-speed limit.

---

## Serial diagnostics

The monitor prints one status line per second, plus events (connect, disconnect, arm, mode, cap):

```
[42s] BLE:OK fails:0 | TX 100 fps (total 4200) | RX bytes 0 frames 0 badCRC 0 lastType 0x00 | thr=0.00 cap=30% arm=0 mode=0
```

| Field | Meaning |
|---|---|
| `BLE` | `OK` connected, `WAIT` waiting for first data, `SCAN` searching |
| `TX fps` | CRSF frames sent in the last second (should be ≈100 while connected) |
| `RX frames / badCRC` | Valid and corrupt CRSF frames received on the RX pin |
| `lastType` | Type byte of the last received frame (telemetry from the FC) |
| `thr`, `cap`, `arm`, `mode` | Current control state |

### Loopback self-test (no flight controller)

Jumper GPIO17 to GPIO16, connect the controller and watch the status line. `TX fps` should be about 100 and `RX frames` should climb at the same rate with `badCRC 0`. That confirms the frame packing and CRC.

### Verifying on the flight controller

1. Mission Planner → **Setup → Mandatory Hardware → Radio Calibration**: the bars should follow the sticks and centre near 1500.
2. **Flight Data → Status** tab: `ch1in` to `ch10in` follow your inputs.
3. **Messages** tab: shows "Armed" or the reason arming was refused.
4. `ch1out` to `ch4out` rise above 1000 once armed and throttled.

---

## Safety

- **Remove propellers** for every bench test. Armed motors idle above zero even at zero throttle.
- Test the failsafe: switch the controller off and confirm the flight controller reports an RC failsafe.
- Do not fly with arming checks disabled.
- Bluetooth is short range and shares the 2.4 GHz band. Do not treat this as a primary control link for a real flight.

## Troubleshooting

| Symptom | Check |
|---|---|
| `BLE: SCAN` forever | Controller must be in pairing mode, not linked elsewhere, and have BLE support |
| `TX fps` is 0 while `BLE: OK` | Firmware or loop problem; reflash |
| `TX fps` ≈ 100 but no channels in Mission Planner | TX/RX swapped, no common GND, wrong `SERIALn_PROTOCOL`, FC not rebooted, or the build lacks the CRSF driver |
| Throttle moves the wrong way | Flip the sign of `-ly` in `loop()` |
| Won't arm | Read the Messages tab; on a bench board see the pre-arm note above |
| Settings revert after reboot | Parameter storage problem on the FC, not the bridge |

## Roadmap

- SBUS and MAVLink `RC_CHANNELS_OVERRIDE` output modes
- Stick expo and per-axis trim
- Haptic (rumble) feedback for arm and failsafe events
- Telemetry back to the controller (battery, mode)

## Credits

- [asukiaaa/XboxSeriesXControllerESP32_asukiaaa](https://github.com/asukiaaa/XboxSeriesXControllerESP32_asukiaaa) for the Xbox BLE driver and [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) for the BLE stack
- [ArduPilot](https://ardupilot.org/) and the [CRSF protocol](https://github.com/tbs-fpv/tbs-crsf-spec) ecosystem
- Xbox is a trademark of Microsoft. This project is not affiliated with Microsoft, ExpressLRS or ArduPilot.

## License

MIT. See [LICENSE](LICENSE).

**Author:** Vaishnav.K.V
