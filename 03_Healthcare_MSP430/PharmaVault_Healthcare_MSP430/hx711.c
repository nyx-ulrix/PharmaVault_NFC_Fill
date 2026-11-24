/* HX711 load-cell amplifier driver. See hx711.h for wiring and timing. */
#include <msp430.h>

#include "hx711.h"

#define HX_DT BIT5                 /* P1.5: data out from the HX711. */
#define HX_SCK BIT0                /* P7.0: clock to the HX711. */
#define HX_TICK_ACLK 3276U         /* 3277 / 32768 Hz = 100.0 ms sampling tick. */
#define HX_MISSING_TICKS 10U       /* ~1 s without data ready -> report the scale absent. */

static volatile bool sample_due;   /* Set by the Timer0_A ISR, cleared by the foreground. */
static uint8_t missed_ticks = HX_MISSING_TICKS;

/* Set up the GPIOs and Timer0_A. SCK idles low: holding it high >60 us powers the HX711 down. */
void hx711_init(void)
{
    P7SEL &= ~HX_SCK;              /* Plain GPIO, not the ADC input (J2 open, VR2 disconnected). */
    P7OUT &= ~HX_SCK;              /* Idle low keeps the chip awake. */
    P7DIR |= HX_SCK;
    P1SEL &= ~HX_DT;
    P1DIR &= ~HX_DT;               /* DT is driven by the HX711. */
    P1REN |= HX_DT;                /* Weak pull-up so a missing module reads "not ready". */
    P1OUT |= HX_DT;
    TA0CCR0 = HX_TICK_ACLK;        /* Second hardware timer: 100 ms sampling period. */
    TA0CCTL0 = CCIE;
    TA0CTL = TASSEL__ACLK | MC__UP | TACLR;
}

/* Clock out 24 data bits MSB first, then one extra pulse for channel A gain 128.
 * Interrupts are held off for ~60 us so no ISR can stretch an SCK-high phase into power-down. */
static int32_t read_24_bits(void)
{
    uint32_t value = 0;
    uint8_t bit;
    const uint16_t sr = __get_SR_register();
    __disable_interrupt();
    for (bit = 0; bit < 24U; ++bit) {
        P7OUT |= HX_SCK;           /* Rising edge shifts the next bit onto DT. */
        __delay_cycles(4);         /* ~1 us high (min 0.2 us, max 50 us). */
        value = (value << 1) | ((P1IN & HX_DT) ? 1UL : 0UL);
        P7OUT &= ~HX_SCK;
        __delay_cycles(4);
    }
    P7OUT |= HX_SCK;               /* 25th pulse: next conversion = channel A, gain 128. */
    __delay_cycles(4);
    P7OUT &= ~HX_SCK;
    if (sr & GIE) __enable_interrupt();
    if (value & 0x800000UL) value |= 0xFF000000UL;   /* Sign-extend two's complement. */
    return (int32_t)value;
}

bool hx711_poll(int32_t *raw)
{
    if (!sample_due) return false;
    sample_due = false;
    if (P1IN & HX_DT) {            /* DT high: conversion not ready (or module missing). */
        if (missed_ticks < HX_MISSING_TICKS) ++missed_ticks;
        return false;
    }
    missed_ticks = 0;
    *raw = read_24_bits();
    return true;
}

bool hx711_present(void)
{
    return missed_ticks < HX_MISSING_TICKS;
}

/* Timer0_A CCR0 ISR: flag a sample and wake the CPU from LPM0. */
#if defined(__TI_COMPILER_VERSION__)
#pragma vector=TIMER0_A0_VECTOR
__interrupt void hx711_timer_isr(void)
#else
void __attribute__((interrupt(TIMER0_A0_VECTOR))) hx711_timer_isr(void)
#endif
{
    sample_due = true;
    __bic_SR_register_on_exit(LPM0_bits);
}
