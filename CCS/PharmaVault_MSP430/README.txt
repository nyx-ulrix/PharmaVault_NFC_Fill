PHARMAVAULT - MSP430F5529

NFC SEQUENCE 1 - 2026-11-08
Use this with the companion ESP32 NFC + fill firmware 1. It retains UART
fix 1 and adds reader heartbeat, card-removal events, stale-tap expiry and
LCD messages for the automatic motor sequence. Pins/baud rates are unchanged.
The 9600-baud debug console prints:
  PHARMAVAULT MSP430 ready (NFC sequence 1; UART fix 1)
If this project is already imported, replace its main.c, rc522.c AND rc522.h
with these versions, then Build, Debug/load and Resume. Do not add duplicate
source files. Alternatively load the precompiled Firmware/PharmaVault_MSP430.out.
This feature update requires the new firmware on BOTH MCUs.

READY SETTINGS
Temperature is already configured for the supplied 10 kohm NTC, Beta 3950 K,
and your 10 kohm fixed divider resistor. No thermistor code edit is needed.
This uses the existing wiring: NTC on P7.0/A12 (J2 open), DHT11 on P1.5,
RC522 on UCB0 SPI, Booster LCD/LEDs, and ESP32 UART on P3.3/P3.4 at 9600.

UPLOAD IN CODE COMPOSER STUDIO
1. Extract the entire ZIP first.
2. In CCS, use File > Import Project (classic CCS: File > Import >
   Code Composer Studio > CCS Projects). Select this PharmaVault_MSP430
   folder. Import the project named PharmaVault_MSP430.
3. Connect the MSP430F5529 LaunchPad's eZ-FET/debug USB port to the laptop.
4. Select this project, Build, then Debug. Once loading finishes, press
   Resume/Run. The sensor node starts automatically.

The project includes its device, compiler options, linker script and debug
target. It requires TI MSP430 compiler 21.6.x and MSP430 device support.
The supplied build used compiler 21.6.2.LTS.

PREBUILT FILE: NO SOURCE BUILD REQUIRED
Firmware/PharmaVault_MSP430.out is already compiled for the configuration
above. In CCS Target Configurations, open targetConfigs/MSP430F5529.ccxml,
launch that target, connect to the MSP430, then use Load Program to select
the supplied .out. Resume/Run after programming. Menu positions vary by CCS
version. The .hex is the equivalent Intel HEX flash image for tools that
accept that format; use the .out with CCS.

IF CCS DOES NOT RECOGNIZE THE PROJECT FOLDER
Import the file ImportAlternative/PharmaVault_MSP430.projectspec instead.
It asks CCS to create the same project from the included sources and
settings. Import either the folder or the projectspec, not both together.

OPTIONAL SERIAL READINGS
The eZ-FET Application UART port prints readings at 9600 baud, 8N1.
NTC_ADC is the raw divider reading, temp_x100 is the NTC temperature in
hundredths of a degree Celsius, and rh_x100 is humidity in hundredths of
a percent. The DHT11 temperature is also printed as a diagnostic only.
NTC temperature and DHT11 humidity are sent to the ESP32 dashboard.

VALIDATION
The supplied source compiled and linked without warnings with TI MSP430
C compiler 21.6.2.LTS. The ELF .out and Intel .hex are from that build.
The CCS metadata was adapted from a TI-supplied managed-build project;
the fallback projectspec validates against the installed TI CCS schema.
The local CCS/Java launcher failed before starting a workspace, so GUI
import and hardware programming were not exercised here. Sensors and
physical timing still need to be checked on your connected boards.

The 20-30 C / maximum 60% RH warnings are demonstration thresholds.
The ESP32 owns the dashboard, calibrated motor positions and tap-dispensing
mode. This MSP firmware sends a card presentation; when tap dispensing is
enabled, the ESP32 may start that card's complete automatic motor sequence.
Only a clean no-response scan lasting about one second confirms removal;
RF collision/selection errors do not. An unsent tap expires after about500ms.
Reader restart or reconnect pauses tap dispensing on the ESP32.
