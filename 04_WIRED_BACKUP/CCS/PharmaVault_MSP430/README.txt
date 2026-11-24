PHARMAVAULT MSP430F5529 - WEEKLY REFILL 1 (2026-11-13)

Use this with the companion weekly ESP32 release. Update BOTH boards.
The MSP430 reads the same thermistor, DHT11 and RC522 and drives the same
Booster LCD/LEDs. Existing pins and baud rates remain unchanged.

WHAT A CARD DOES
Tapping any supported card requests the current seven-day cycle status from
the ESP32. No card assignment, daily card tap or medicine name is required
to run the schedule. The card is an optional information lookup only.

LCD first line          LCD second line
Load 7 days             Confirm in web        (weekly refill not confirmed)
Week starts soon        Timer unchanged       (before first scheduled day)
Week day N of 7         Timer unchanged       (scheduled day 1..7)
Refill all 7days        Confirm in web        (weekly refill is due)
Set clock in web        Timer saved           (clock needs synchronization)

The first refill line uses "7days" to fit the 16-column LCD.
The day counts scheduled days, including missed days, not motor completions.
The ESP32 dashboard is the place to confirm an actual weekly refill and
parking alignment. An NFC tap never confirms a refill, resets the week,
changes the saved daily time or starts the motor.
The card view lasts about eight seconds against routine ESP status traffic.
A new card, hardware/link failure or new environment warning may replace it.
There is no paired dose-log request to overwrite the status with NO_LOG.
Remove the card for at least one second before tapping it again.

WEEKLY OPERATION
Compartments 1..7 hold medicines for their assigned scheduled days; compartment
8 is the empty parking position. The timer and weekly state live on ESP32.
After a missed day, follow the dashboard recovery instructions before using
the motor again. Do not move past a loaded missed compartment to catch up.
NFC status does not measure remaining stock or prove pills were delivered.

STEPPER STATUS ON LCD
DOSE_READY: Dose ready / Compartment N (scheduled time; no movement yet).
DOSE_MOVING: Dispensing steps / Compartment N (hand accepted).
DOSE_STEPS_DONE: Steps completed / Compartment N (command ended).
MOTOR_FAULT: Motor fault / Inspect carousel.
Completion is a step command result, not proof pills fell or were taken.

USB DEBUG OUTPUT (9600 BAUD, 8N1)
  PHARMAVAULT MSP430 ready (weekly 1; UART fix 1)
  UID <card UID>
  Week day N: ACTIVE
Other possible status words: SETUP, REFILL, CLOCK.
Existing NTC_ADC/temp_x100 and DHT diagnostic_temp_x100/rh_x100 continue.
The DHT temperature is diagnostic; NTC temperature is the main temperature.

LOAD THE SUPPLIED BUILD
1. Extract the complete release before opening its files.
2. If your CCS target already works, load
     Firmware/PharmaVault_MSP430.out
   through the existing MSP430F5529 target and Resume/Run.
3. To rebuild, import this project folder or the fallback
     ImportAlternative/PharmaVault_MSP430.projectspec
   (choose one import method). Build, Debug/load, then Resume/Run.
4. If updating an existing project, replace main.c, dht11.c/.h, rc522.c/.h,
   PvProtocol.h and ThermistorConfig.h with this release's files. Keep only
   one copy of each source. Do not import the Firmware folder as a project.

The target uses TI MSP430 C compiler 21.6.x, C99, small code/data model.
The supplied .out and Intel .hex are built using 21.6.2.LTS.
The prebuilt .out is the recommended file for CCS Load Program.

UNCHANGED SENSOR SETUP
  NTC: 10k at 25C, Beta 3950. Target 3.3V -> fixed 10k -> P7.0/A12 -> NTC -> GND.
       Leave J2 open; the NTC is the lower leg toward ground.
  DHT11 DATA: P1.5. NTC and DHT use target 3.3V/GND.
  RC522: UCB0 SPI P3.0 MOSI, P3.1 MISO, P3.2 SCK; SS P2.3; RST P1.6.
  UART: P3.3 TX -> ESP32 GPIO4; ESP32 GPIO17 -> P3.4 RX; common GND.
  Booster LCD H3: existing parallel wiring. LEDs P8.1/P8.2.
Thermistor ADC, DHT timing, scheduler/UART/ADC/DHT ISRs and LPM0 are retained.
The 20-30C/max 60% RH warnings are demonstration thresholds, not a statement
about the storage requirements of any particular medicine.

PROTOCOL
MSP requests: PV1,sequence,TAG,UID with existing CRC framing.
ESP replies:  PV1,sequence,WEEK,day,status
This is five comma-separated fields before the checksum. Day is 0..7;
status is SETUP, ACTIVE, REFILL or CLOCK. Only the currently awaited TAG
sequence can show weekly status. Invalid checksum, sequence, command kind,
field count, day or status is rejected without updating the display.
Legacy REFILL and LOG replies remain recognized for compatibility; this
release's ESP32 uses WEEK and no daily mapping/name requirement.

VALIDATION
See MSP_VALIDATION.txt for native-build and host protocol test evidence.
Code/build tests do not establish sensor wiring, LCD contrast or actual
hardware operation. Test the card/LCD and sensor readings after programming.

TEAM RECORD
The supplied code is AI-assisted. Record real contributors, adaptations,
revision history and each member's reflection in the submitted sources.
