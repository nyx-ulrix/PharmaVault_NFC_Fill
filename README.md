# PharmaVault

A smart pill dispenser in two parts that talk over Wi-Fi:

- **Patient pillbox:** an ESP32-S3 (the "PBesp") with the IR hand sensor, NTC thermistor, DHT11 humidity sensor, a 28BYJ-48 stepper on a ULN2003 driver, the web dashboard, the daily schedule and the seven-day dose journal.
- **Healthcare station:** an MSP430F5529 LaunchPad with the CEG2002 Booster Board (RC522 NFC reader, 16x2 LCD, LEDs), linked by UART to a FireBeetle ESP32-E (the "ESPmsp") that bridges it to the pillbox over Wi-Fi.

```
[Pillbox: sensors + motor + PBesp ESP32-S3]
              <--- PharmaVault Wi-Fi (TCP 4210) --->
[ESPmsp FireBeetle] <-- UART 9600 8N1 --> [MSP430 + RC522 + LCD]
```

The pillbox makes its own Wi-Fi access point, so no router is needed. Each station only needs its own USB power. No wires run between the two stations.

## Repository layout

| Folder | What it is | Board / tool |
| --- | --- | --- |
| `01_PBesp_ESP32S3/PharmaVault_PBesp/` | Pillbox firmware and dashboard | ESP32S3 Dev Module, Arduino IDE |
| `02_ESPmsp_FireBeetle/PharmaVault_ESPmsp/` | Wi-Fi to UART bridge | FireBeetle 2 ESP32-E, Arduino IDE |
| `03_Healthcare_MSP430/PharmaVault_Healthcare_MSP430/` | Healthcare station firmware (CCS project, prebuilt `.out`/`.hex` in `Firmware/`) | MSP430F5529, Code Composer Studio |
| `04_WIRED_BACKUP/` | Earlier wired version (MSP430 and ESP32-S3 joined by UART) | See `04_WIRED_BACKUP/START_HERE.txt` |
| `Validation/` | Build and test records | |

**Editing the dashboard:** the page lives in `PvDashboardHtml.h`. After changing it (or `web/mascot.webp`), run `python 01_PBesp_ESP32S3/PharmaVault_PBesp/web/build_web.py` to regenerate the gzipped copy the board serves. The build fails with a reminder if you forget.

The detailed guides are the `.txt` files in the root: `START_HERE.txt`, `FRESH_PC_INSTALL.txt`, `WIRING.txt`, `TEST_AND_DEMO.txt`, `NORMAL_OPERATION.txt` and `WIRED_BACKUP_RESTORE.txt`.

## Default passwords

| What | Value |
| --- | --- |
| Pillbox Wi-Fi name | `PharmaVault-` + 12 hex digits from the chip (printed on the serial monitor) |
| Pillbox Wi-Fi password | `PharmaVault67` |
| Dashboard | http://pharmavault (or http://192.168.4.1) |
| Dashboard on your home Wi-Fi | http://pharmavault.local |
| Devices at once | 4 on the pillbox Wi-Fi, including the FireBeetle bridge |
| Dashboard login | username `demo`, password `PharmaVault67` |

The password is set in `loadSettings()` in `PharmaVault_PBesp.ino`. To change it, edit that one string (8 to 32 characters, no spaces), upload again, and re-pair the FireBeetle with the new password.

## 1. Install the tools

**Arduino IDE (both ESP32 boards)**

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. In Preferences, add this Boards Manager URL:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In Boards Manager, install **esp32 by Espressif Systems**, version **3.3.12**.

No extra libraries are needed. The DHT driver is included in the sketch folder.

**Code Composer Studio (MSP430)**

1. Install [CCStudio](https://www.ti.com/tool/CCSTUDIO) with MSP430 support.
2. To build from source, use the TI MSP430 compiler **21.6.2.LTS**. You can skip this if you load the prebuilt `Firmware/PharmaVault_Healthcare_MSP430.out`.

## 2. Wire it

Power everything off first. Full details are in `WIRING.txt`.

**Pillbox (ESP32-S3)**

| Part | Connection |
| --- | --- |
| ULN2003 IN1 to IN4 | GPIO5, 6, 7, 8 |
| ULN2003 power | 5 V (USB VBUS) and GND of the S3 |
| IR sensor | VCC 3V3, GND, OUT to GPIO14 (active LOW) |
| NTC 10 kΩ B3950 | GPIO13 (firmware holds it at 3.3 V) to 10 kΩ fixed resistor to junction to NTC to GND. Junction to GPIO1 (ADC1_CH0) |
| DHT11 | VCC 3V3, GND, DATA to GPIO2 |

**Healthcare station (MSP430 + FireBeetle)**

| Part | Connection |
| --- | --- |
| MSP P3.3 TX (H2.4) | FireBeetle GPIO16 RX (D11) |
| FireBeetle GPIO17 TX (D10) | MSP P3.4 RX (H2.3) |
| MSP GND (H2.20 / H2.22) | FireBeetle GND |
| RC522 | 3V3, GND, MOSI P3.0, MISO P3.1, SCK P3.2, SDA/SS P2.3, RST P1.6 |
| LCD | Booster header H3 |

Do not use the FireBeetle's printed TX/RX pins (GPIO1/3), because those are its USB serial.

## 3. Upload the firmware

Plug in one board at a time so you can tell the COM ports apart.

**Pillbox:** open `01_PBesp_ESP32S3/PharmaVault_PBesp/PharmaVault_PBesp.ino` and choose board **ESP32S3 Dev Module**. Turn **USB CDC On Boot** on if you use the S3's native USB port. Upload with **Erase All Flash** off, so the saved schedule and dose history are kept. Then open the Serial Monitor at 115200 baud and expect:

```
PHARMAVAULT PBesp wireless 2 (local sensors)
```

Send `?` at any time to print the Wi-Fi name, the password and the dashboard address.

**Bridge:** open `02_ESPmsp_FireBeetle/PharmaVault_ESPmsp/PharmaVault_ESPmsp.ino` and choose board **FireBeetle 2 ESP32-E** (`esp32:esp32:dfrobot_firebeetle2_esp32e`). Upload, then open the Serial Monitor at 115200 baud with line ending **Newline**. Expect `PHARMAVAULT wireless bridge 1`.

**MSP430 without CCS:** `Firmware/PharmaVault_Healthcare_MSP430_gcc.hex` is built from the current source with TI's MSP430-GCC 9.3.1. Flash it with TI's MSP Flasher: `MSP430Flasher.exe -n MSP430F5529 -w PharmaVault_Healthcare_MSP430_gcc.hex -v -e ERASE_MAIN -z [RESET,VCC]`. The older `.out`/`.hex` are the original CCS build.

**MSP430:** in CCS, use File > Import Projects and pick `03_Healthcare_MSP430/PharmaVault_Healthcare_MSP430` (import it once). Build it, then Run > Debug and press **Resume**. You can also load the prebuilt `.out` from `Firmware/` instead of building. Its console runs at 9600 baud and should show `PHARMAVAULT healthcare MSP430 ready (wireless provider 1)`.

## 4. Pair the bridge (once)

1. On the pillbox Serial Monitor, send `?` and copy the **Wi-Fi name** (`PharmaVault-XXXXXXXXXXXX`).
2. On the FireBeetle Serial Monitor, send `SETUP`.
3. Enter the Wi-Fi name, then the password `PharmaVault67`.
4. Wait for `Pair saved` and `BRIDGE ONLINE`. The pairing is kept through restarts. Send `FORGET` to clear it, or `?` to see the link status.

## 5. Set up the dispenser

1. Connect the laptop to the `PharmaVault-...` Wi-Fi (password `PharmaVault67`). Windows may say "No Internet"; stay connected.
2. Open **http://pharmavault** (type the `http://`, or the browser may search for it instead). If that fails, use http://192.168.4.1 and log in as `demo` / `PharmaVault67`.
3. Sync the clock and set the daily dose time.
4. Confirm the carousel's current compartment. Slot 8 is the empty parking position.
5. Load the seven compartments, record the new pack, and type the Day 1 to Day 7 refill instructions.
6. Enable the schedule.

## Daily use

- At the daily time, that day's compartment becomes READY. The motor does not move yet.
- Hold a hand over the IR sensor for 1.5 s, after it has seen nothing for 1 s. The carousel then turns one compartment.
- Tilt the dispenser so the pills slide out of the exit slot.
- One dose per day only. If no hand comes by the alert time, the dose is marked OVERDUE. After midnight it is marked MISSED, and the motor stays blocked until you realign it.
- After day 7, the dashboard asks for a weekly refill.
- Tap the NFC tag on the RC522 reader to show the week's status and refill instructions on the LCD. A tap never dispenses anything.
- Temperature (NTC) and humidity (DHT11) are read about every 5 s. Warnings show on the dashboard and the LCD.

`TEST_AND_DEMO.txt` covers a quick test mode (10 s READY, 30 s OVERDUE) for demos.

## Troubleshooting

| Problem | Check |
| --- | --- |
| No COM port | Use a data USB cable and check Device Manager > Ports for the USB chip. |
| Dashboard won't load | Laptop on the `PharmaVault-...` Wi-Fi with automatic IP, using `http://pharmavault` or `http://192.168.4.1`. Turn off mobile data so the phone asks the pillbox for the name. |
| `BRIDGE OFFLINE` | FireBeetle powered, paired with the right Wi-Fi name, and password `PharmaVault67`. |
| MSP LCD shows stale data | FireBeetle link down, or the MSP430 stopped at `main` in the debugger (press Resume). |
| Board resets while the motor turns | The USB supply is too weak for the motor. Use a better cable or port. |
