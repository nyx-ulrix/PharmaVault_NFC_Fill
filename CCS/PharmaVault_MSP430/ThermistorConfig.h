#ifndef THERMISTOR_CONFIG_H
#define THERMISTOR_CONFIG_H

/* Preconfigured from the user's supplied X-JM0021A / MF52 specification:
 * NTC = 10 kohm at 25 C, Beta = 3950 K. Fixed divider resistor = 10 kohm.
 * These supplied component specifications have not been independently measured.
 * Wiring: H2.1 / target 3.3 V -> fixed resistor -> H2.28 / P7.0 / A12 node
 *         -> NTC -> GND. J2 must be OPEN to disconnect the fitted VR2.
 */
#define NTC_FIXED_RESISTOR_OHMS 10000.0f
#define NTC_R25_OHMS            10000.0f
#define NTC_BETA_K              3950.0f

#endif
