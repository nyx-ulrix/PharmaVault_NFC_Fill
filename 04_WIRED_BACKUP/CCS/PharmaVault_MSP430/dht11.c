/* Author: Choo Zhen Da (CZD). Wired backup version, kept as a fallback. Revised 2026-11-25. */
#include "dht11.h"
#include <msp430.h>

/* Timer0_A uses 1.048576 MHz SMCLK. Constants fit 32-bit arithmetic. */
#define US_TICKS(us) ((uint16_t)(((uint32_t)(us) * 10486UL + 5000UL) / 10000UL))
#define DATA_PIN BIT5                 /* P1.5 is TA0.CCI4A on F5529. */

typedef enum {
    IDLE,
    START_LOW,
    REPLY_LOW,
    REPLY_HIGH,
    FIRST_BIT_LOW,
    BIT_HIGH,
    BIT_LOW
} CapturePhase;

static volatile CapturePhase phase;
static volatile Dht11Result result;
static volatile uint8_t raw[5];
static uint8_t bit_count;
static uint16_t edge;

/* Leave DATA released and start the continuous capture timebase. */
void dht11_init(void)
{
    P1SEL &= ~DATA_PIN;
    P1DIR &= ~DATA_PIN;
    P1OUT |= DATA_PIN;
    P1REN |= DATA_PIN;              /* Existing MCU pull-up supports the 3.3 V module. */
    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR;
    TA0CCTL1 = TA0CCTL2 = TA0CCTL4 = 0;
    phase = IDLE;
    result = DHT11_PENDING;
}

/* Pull DATA low for 21 ms. CCR1 will release it while the foreground runs. */
bool dht11_start(void)
{
    uint8_t i;
    if (phase != IDLE || result != DHT11_PENDING) return false;

    for (i = 0; i < 5; ++i) raw[i] = 0;
    bit_count = 0;
    P1REN &= ~DATA_PIN;
    P1OUT &= ~DATA_PIN;
    P1DIR |= DATA_PIN;
    phase = START_LOW;
    TA0CCR1 = TA0R + US_TICKS(21000);
    TA0CCTL1 = CCIE;
    return true;
}

/* End capture on success, timeout or malformed pulse. Called only by ISR. */
static inline void finish_capture(Dht11Result status)
{
    TA0CCTL1 = TA0CCTL2 = TA0CCTL4 = 0;
    P1SEL &= ~DATA_PIN;
    P1DIR &= ~DATA_PIN;
    P1OUT |= DATA_PIN;
    P1REN |= DATA_PIN;
    phase = IDLE;
    result = status;
}

/* Release the open-drain data line and arm both-edge capture with a deadline. */
static inline void release_start(void)
{
    TA0CCTL1 = 0;
    phase = REPLY_LOW;
    P1DIR &= ~DATA_PIN;
    P1OUT |= DATA_PIN;
    P1REN |= DATA_PIN;
    P1SEL |= DATA_PIN;
    TA0CCTL4 = CM_3 | CCIS_0 | SCS | CAP | CCIE;
    TA0CCR2 = TA0R + US_TICKS(10000);
    TA0CCTL2 = CCIE;
}

/* Consume one captured edge. Tick subtraction handles TA0R rollover. */
static inline void read_edge(void)
{
    const uint16_t now = TA0CCR4;
    const uint16_t width = (uint16_t)(now - edge);
    const uint16_t ctl = TA0CCTL4;
    const bool high = (ctl & CCI) != 0;

    if (ctl & COV) { finish_capture(DHT11_FAILED); return; }

    switch (phase) {
    case REPLY_LOW:
        if (!high) { edge = now; phase = REPLY_HIGH; }
        break;
    case REPLY_HIGH:
        if (!high || width < US_TICKS(55) || width > US_TICKS(120)) {
            finish_capture(DHT11_FAILED);
        } else {
            edge = now;
            phase = FIRST_BIT_LOW;
        }
        break;
    case FIRST_BIT_LOW:
        if (high || width < US_TICKS(55) || width > US_TICKS(125)) {
            finish_capture(DHT11_FAILED);
        } else {
            edge = now;
            phase = BIT_HIGH;
        }
        break;
    case BIT_HIGH:
        if (!high || width < US_TICKS(28) || width > US_TICKS(85)) {
            finish_capture(DHT11_FAILED);
        } else {
            edge = now;
            phase = BIT_LOW;
        }
        break;
    case BIT_LOW:
        if (high || width < US_TICKS(14) || width > US_TICKS(100) ||
            (width > US_TICKS(43) && width < US_TICKS(53))) {
            finish_capture(DHT11_FAILED);
        } else {
            const uint8_t i = bit_count >> 3;
            raw[i] = (uint8_t)((raw[i] << 1) | (width >= US_TICKS(53)));
            ++bit_count;
            edge = now;
            if (bit_count == 40) finish_capture(DHT11_SAMPLE);
            else phase = BIT_HIGH;
        }
        break;
    default:
        finish_capture(DHT11_FAILED);
        break;
    }
}

/* Return one complete, checksum-verified DHT11 sample to the caller. */
Dht11Result dht11_take(int32_t *temperature_x100, uint16_t *humidity_x100)
{
    const Dht11Result ready = result;
    uint16_t rh;
    int32_t temperature;
    if (ready == DHT11_PENDING) return DHT11_PENDING;
    result = DHT11_PENDING;
    if (ready != DHT11_SAMPLE || raw[1] > 9 || (raw[3] & 0x70) ||
        (raw[3] & 0x0f) > 9 ||
        (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]) != raw[4]) {
        return DHT11_FAILED;
    }
    rh = (uint16_t)raw[0] * 100U + (uint16_t)raw[1] * 10U;
    temperature = (int32_t)raw[2] * 100L;
    /* DHT11 negative values encode -(integer + 1) plus the decimal portion. */
    if (raw[3] & 0x80) temperature = -100L - temperature;
    temperature += (int32_t)(raw[3] & 0x0f) * 10L;
    if (rh > 10000U || temperature < -2000L || temperature > 6000L) return DHT11_FAILED;
    *temperature_x100 = temperature;
    *humidity_x100 = rh;
    return DHT11_SAMPLE;
}

/* Timer function 2: release start pulse, capture bit timing and time out. */
#pragma vector=TIMER0_A1_VECTOR
__interrupt void dht11_timer_isr(void)
{
    switch (__even_in_range(TA0IV, TA0IV_TAIFG)) {
    case TA0IV_TACCR1: release_start(); break;
    case TA0IV_TACCR2: finish_capture(DHT11_FAILED); break;
    case TA0IV_TACCR4: read_edge(); break;
    default: break;
    }
    if (result != DHT11_PENDING) __bic_SR_register_on_exit(LPM0_bits);
}
