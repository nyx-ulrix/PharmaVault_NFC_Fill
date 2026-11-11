#ifndef PHARMAVAULT_DHT11_H
#define PHARMAVAULT_DHT11_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DHT11_PENDING,
    DHT11_SAMPLE,
    DHT11_FAILED
} Dht11Result;

/* Initialize Timer0_A using the board's 1.048576 MHz SMCLK. */
void dht11_init(void);

/* Start a measurement if idle. Allow >2 s between calls. */
bool dht11_start(void);

/* Consume a complete sample. Outputs are Celsius x100 and relative humidity x100. */
Dht11Result dht11_take(int32_t *temperature_x100, uint16_t *humidity_x100);

#endif
