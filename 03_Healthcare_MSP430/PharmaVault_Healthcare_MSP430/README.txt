PHARMAVAULT HEALTHCARE STATION - MSP430F5529
Wireless provider release1, 17 November2026

THIS IS THE NEW HEALTHCARE-STATION FIRMWARE
The MSP430 now owns only the RC522 reader and fitted Booster LCD/LEDs.
Temperature, humidity, IR and the motor belong to the separate ESP32-S3 pillbox.
The FireBeetle ESP32-E beside the MSP430 relays remote messages over Wi-Fi.
Use this project with the accompanying independent-stations S3 and bridge
sketches. The wired backup's older MSP430 firmware has different sensor roles.

INSTALL IN CCS
1. Extract the entire package to a normal local folder before opening it.
2. Import this project folder using CCS's project import command. Its project
   name is PharmaVault_Healthcare_MSP430, distinct from the old wired project.
   If that method does not recognize the project, import ONLY:
   ImportAlternative/PharmaVault_Healthcare_MSP430.projectspec
   Do not add both imports or import Firmware as a project.
3. Select MSP430F5529 with the LaunchPad USB/eZ-FET connection. The supplied
   compiler setting is TI MSP43021.6.2.LTS, C99, small code/data model.
4. Build the project, start Debug/load and Resume/Run.
   Only main.c and rc522.c are source files to compile. No external DHT library
   or ThermistorConfig.h is required by this new MSP430 role.
5. If your CCS target is already configured, the prebuilt target-compiled
   Firmware/PharmaVault_Healthcare_MSP430.out can instead be loaded directly
   through Load Program, then Resume/Run. The .hex is also included.
6. Connect the MSP430 application/backchannel COM port at9600 baud,8N1.
   After reset, look for exactly:
   PHARMAVAULT healthcare MSP430 ready (wireless provider 1)

LOCAL CONNECTIONS - POWER OFF BEFORE CHANGING WIRES
MSP430 P3.3 / Booster H2.4 TX -> FireBeetle D11 / GPIO16 RX2
FireBeetle D10 / GPIO17 TX2 -> MSP430 P3.4 / Booster H2.3 RX
MSP430 GND / Booster H2.20 orH2.22 -> FireBeetle GND
UART is9600 baud,8N1,3.3V logic.
Do not use FireBeetle printed TX/RX pins(GPIO1/3); those serve USB serial.

RC522 remains on the MSP430:
VCC -> MSP4303.3V; GND -> MSP430GND
MOSI -> P3.0 / H2.15; MISO -> P3.1 / H2.14
SCK -> P3.2 / H2.7; SDA/SS -> P2.3 / H2.12; RST -> P1.6 / H2.5
IRQ remains unused. LCD remains on its fitted Booster H3 connection.
No new buttons/switches are used. LEDs remain P8.1/P8.2.

Remove the old NTC connection from P7.0/A12 and the DHT data connection from
P1.5. Move their power, ground and signal wiring to the pillbox as specified
in the release's complete wiring guide. This firmware does not sample them
locally. The old direct MSP430-to-S3 UART wires are removed.

WHAT THE HEALTHCARE LCD SHOWS
- Idle view alternates the pillbox's latest temperature/humidity with status.
- PB data offline / Check wireless: an expected remote stream is stale.
- Storage warning orPB sensor fault: remote environment flags need attention.
- Missed/overdue / Check dashboard: at least one real dated dose remains
  overdue/uncompleted; use the dashboard history to identify the date.
- Refill all7days: the weekly refill reminder is active.
- TEST overdue: the repeatable countdown demonstration is overdue. It is
  explicitly a test, not an extra real missed-day record.
- Values/status expire after about16s without an update. Old numbers are
  replaced by unavailable/stale messages rather than presented as live data.

NFC REFILL GUIDE
1. Configure Day1..Day7 refill instructions on the paired pillbox dashboard.
   Each day supports up to48 printable ASCII characters. Unset text is Not set.
2. Tap any supported kit card. A card is an information request for the
   currently paired pillbox; it is not a patient/medicine assignment or login.
3. The MSP430 receives weekly status, then requests the seven day instructions
   in order. During this short fetch it shows Reading refill / DayN of7.
4. After all seven are received, the LCD shows DayN refill on the first row.
   The second row shows16 characters at a time, changing about every2.5s.
   All seven days appear once, then the normal environment/status view resumes.
5. Remove the card for at least one second before tapping again to repeat.
   A new card cancels the old list. New warnings can interrupt the list; a
   steady warning does not continuously cover the text on a subsequent tap.
6. The card never moves the motor, confirms a refill, changes the timer or
   resets weekly progress. Refill physically, then confirm it on the dashboard.
   The instructions do not measure remaining stock or prove a pill was taken.

SERIAL DIAGNOSTICS AT9600
PB_ENV temp_x100=2450,rh_x100=5300,flags=0 ->24.50C,53.00%RH
PB_BOX day=2,state=READY,overdue=0,refill=0 ->current pillbox status
UID <card UID>
Week day N: ACTIVE (also SETUP,REFILL,CLOCK)
Refill day N: <configured instruction>
No reply/Refill link error ->check bridge pairing, local UART cross-connections
and the S3 wireless firmware. Retap after the link is restored.

IMPLEMENTATION
Timer1_A supplies the10ms tick; UART RX/TX use interrupt buffers; the CPU uses
LPM0 between jobs. No local ADC conversion, DHT capture ISR or motor is active.
Only matching command sequences and requested days can update refill text.
Each MED request retains the existing700ms retry interval/three-attempt limit.
Incoming ENV/BOX frames use sequence0 and strict shape/range/CRC checks.
MED text is hex-encoded on the wire, so spaces and punctuation cannot break
the existing comma-delimited protocol. Text is decoded only after validation.

VALIDATION AND LIMITS
The included .out/.hex were freshly built with the TI MSP430 target compiler.
Actual-source protocol, LCD pagination, retry and freshness tests passed on
the host. See MSP_VALIDATION.txt. Physical flashing, LCD contrast, RF card
reads, radio range, motor power and sensor placement still require bench tests.
