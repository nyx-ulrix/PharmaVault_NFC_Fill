/* HX711 load-cell amplifier (XFW-HX711 module) driver for the MSP430F5529.
 *
 * Wiring (module powered from the LaunchPad 3.3 V rail so logic levels match):
 *   HX711 VCC -> 3V3      HX711 GND -> GND
 *   HX711 DT  -> P1.5 (Booster H2.38)   data-ready / serial data, input
 *   HX711 SCK -> P7.0 (Booster H2.28)   clock out; Booster jumper J2 must stay open
 *
 * Timer0_A raises a 100 ms sampling tick (the HX711 module samples at 10 Hz).
 * Each tick the foreground reads one 24-bit result if the chip reports data
 * ready (DT low), then sends a 25th clock to select channel A, gain 128.
 */
#ifndef PHARMAVAULT_HX711_H
#define PHARMAVAULT_HX711_H

#include <stdbool.h>
#include <stdint.h>

/* Configure the two GPIOs and start the Timer0_A 100 ms sampling tick.
 * Must run after the clocks are configured (Timer0_A uses ACLK = 32.768 kHz). */
void hx711_init(void);

/* Read one sample if a sampling tick has elapsed and the HX711 has data ready.
 * raw: receives the signed 24-bit conversion, sign-extended to 32 bits.
 * Returns true only when *raw holds a new reading. Never blocks waiting. */
bool hx711_poll(int32_t *raw);

/* True if the HX711 answered within the last ~1 s (DT went low on time). */
bool hx711_present(void);

#endif
