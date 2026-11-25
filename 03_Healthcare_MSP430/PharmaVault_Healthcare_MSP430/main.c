/* Author: Clifton Ng Jun Heng (CN). Main loop, Timer1_A tick and NFC scheduling, UART, LCD pages. Revised 2026-11-25. */
/* PHARMAVAULT healthcare station - wireless provider release 1 (2026-11-17).
 * MSP430F5529 / TI CCS C99. NFC and Booster LCD/LEDs are local to this station.
 * The FireBeetle ESP32-E bridge connects on UCA0 P3.3 TX/P3.4 RX at9600.
 * All environmental sensors, IR and the motor are at the separate ESP32-S3 pillbox.
 * Remote ENV and BOX frames expire after16s; old readings are never shown as live.
 * Any supported NFC card requests the paired pillbox's weekly/refill instructions.
 * Seven medicine texts are fetched sequentially and shown in16-character pages.
 * Timer1_A, UART RX/TX ISRs and LPM0 retain interrupt-driven low-power operation.
 * No local ADC, DHT input or motor is initialized by this healthcare firmware.
 * A complete refill list is held in RAM for an explicitly marked offline view.
 * Revisions and team attribution: see this project's Git history. AI assisted.
 */
#include <msp430.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PvProtocol.h"
#include "rc522.h"

#define LCD_RS BIT4                    /* H3 control lines on P6. */
#define LCD_RW BIT5
#define LCD_EN BIT6
#define LED_OK BIT1                    /* Booster LED1 P8.1, LED2 P8.2. */
#define LED_WARN BIT2
#define RX_CAPACITY 256U               /* Eight-bit ring indices wrap naturally. */
#define COMMAND_CAPACITY 8U
#define NFC_PERIOD 12UL                /* About 120 ms, in Timer1_A ticks. */
#define ACK_TIMEOUT 70UL              /* About 700 ms including frame time. */
#define CARD_REMOVE_TICKS 100UL       /* Require about one second of absence. */
#define READER_HEARTBEAT 500UL        /* Reader presence status about every 5s. */
#define TAG_QUEUE_MAX_AGE 50UL        /* Expire an old unsent lookup instead of showing it late. */
#define REFILL_HOLD_TICKS 6000UL     /* Maximum whole-list view; individual pages use250 ticks. */
#define REMOTE_FRESH_TICKS 1600UL    /* About16 seconds without new remote data. */
#define REFILL_PAGE_TICKS 250UL      /* About2.5 seconds per16-character page. */
#define ADC_TEMP_INVALID (-32768L)
#define DHT_HUMIDITY_INVALID 65535U

#define DEBUG_UART 1                   /* eZ-FET backchannel at 9600 baud. */

typedef enum { CMD_ENV, CMD_TAG, CMD_LOGREQ, CMD_CLEAR, CMD_READER, CMD_MED } CommandKind;
typedef struct {
    CommandKind kind;
    uint16_t seq;
    uint32_t queued_tick;
    char arguments[32];
} Command;

static volatile uint32_t ticks;
static volatile uint8_t rx_head, rx_tail;
static volatile bool rx_fault;
static uint8_t rx_ring[RX_CAPACITY];
static char tx_frame[PV_FRAME_SIZE];
static volatile uint8_t tx_length, tx_index;

static Command commands[COMMAND_CAPACITY];
static uint8_t command_head, command_count, attempts;
static uint16_t next_sequence;
static uint32_t sent_tick;
static bool waiting_ack;

static bool reader_online;
static int32_t temperature_x100 = ADC_TEMP_INVALID;
static uint16_t humidity_x100 = DHT_HUMIDITY_INVALID;
static uint8_t flags = 4, remote_warning_mask;
static uint32_t next_nfc, lcd_hold_until;
static uint32_t led_until;
static char held_uid[21];
static bool absence_pending, absence_confirmed, reader_resync_pending;
static uint32_t absence_started, next_reader_heartbeat;
static bool refill_view_active;
static uint32_t refill_hold_until;
static bool remote_environment_seen, remote_box_seen;
static uint32_t remote_environment_tick, remote_box_tick;
static uint8_t box_day;
static char box_state[16] = "SETUP";
static bool box_overdue, box_refill;
static char refill_text[7][49];
static bool refill_fetch_active, refill_request_pending;
static uint8_t refill_next_day, refill_page_day, refill_page_chunk;
static bool idle_status_page;
/* Last complete WEEK + Day 1..7 answer. Shown whenever a live tap lookup fails. RAM only. */
static char pending_week[17], cached_week[17], cached_text[7][49];
static bool cache_valid, showing_saved_copy;

/* Configure nominal MCLK 4.194304 MHz, SMCLK 1.048576 MHz, ACLK 32.768 kHz. */
static void clocks_init(void)
{
    uint8_t attempt;
    WDTCTL = WDTPW | WDTHOLD;
    UCSCTL6 |= XT1OFF | XT2OFF;       /* Internal REFO supplies the reference. */
    UCSCTL3 = SELREF__REFOCLK;
    __bis_SR_register(SCG0);
    UCSCTL0 = 0;
    UCSCTL1 = DCORSEL_4;
    UCSCTL2 = FLLD_1 | 127;          /* DCOCLKDIV = 32768 Hz × (127 + 1). */
    UCSCTL4 = SELA__REFOCLK | SELS__DCOCLKDIV | SELM__DCOCLKDIV;
    UCSCTL5 = DIVS__4;
    __bic_SR_register(SCG0);
    __delay_cycles(140000);          /* Allow the FLL to settle. */
    for (attempt = 0; attempt < 64; ++attempt) {
        UCSCTL7 &= ~(XT2OFFG | XT1LFOFFG | DCOFFG);
        SFRIFG1 &= ~OFIFG;
        if (!(SFRIFG1 & OFIFG)) break;
    }
}

/* Read the 32-bit interrupt tick atomically on the 16-bit MCU. */
static uint32_t tick_now(void)
{
    const uint16_t sr = __get_SR_register();
    uint32_t value;
    __disable_interrupt();
    value = ticks;
    if (sr & GIE) __enable_interrupt();
    return value;
}

/* Compare near-term deadlines across a 32-bit tick rollover. */
static bool due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/* Drive a four-bit LCD nibble, leaving RS and RW unchanged. */
static void lcd_nibble(uint8_t nibble)
{
    P6OUT = (P6OUT & 0xf0) | (nibble & 0x0f);
    P6OUT |= LCD_EN;
    __delay_cycles(6);
    P6OUT &= ~LCD_EN;
}

/* Send a command (data=false) or one character (data=true). */
static void lcd_write(uint8_t value, bool data)
{
    if (data) P6OUT |= LCD_RS;
    else P6OUT &= ~LCD_RS;
    lcd_nibble(value >> 4);
    lcd_nibble(value);
    __delay_cycles(230);
}

/* Initialize the fitted H3 LCD in write-only 4-bit, 16×2 mode. */
static void lcd_init(void)
{
    P6SEL &= ~0x7f;
    P6DIR |= 0x7f;
    P6OUT &= ~(LCD_RW | LCD_RS | LCD_EN);
    __delay_cycles(170000);
    lcd_nibble(3); __delay_cycles(21000);
    lcd_nibble(3); __delay_cycles(1000);
    lcd_nibble(3); __delay_cycles(1000);
    lcd_nibble(2);
    __delay_cycles(230);
    lcd_write(0x28, false);          /* 4-bit bus, two display lines. */
    lcd_write(0x08, false);
    lcd_write(0x01, false);
    __delay_cycles(9000);            /* Clear needs a longer busy interval. */
    lcd_write(0x06, false);
    lcd_write(0x0c, false);
}

/* Fill one display row, truncating long text and padding old characters. */
static void lcd_line(uint8_t address, const char *text)
{
    uint8_t i;
    lcd_write(address, false);
    for (i = 0; i < 16; ++i) {
        char c = *text;
        if (c) ++text;
        lcd_write((c >= ' ' && c <= '~') ? (uint8_t)c : (uint8_t)' ', true);
    }
}

/* Display an event. hold is the time before idle environment view resumes. */
static void display(const char *top, const char *bottom, uint32_t hold)
{
    lcd_line(0x80, top);
    lcd_line(0xc0, bottom);
    lcd_hold_until = tick_now() + hold;
}

/* Configure both USCI UARTs. The debug UART uses the eZ-FET jumpers. */
static void uart_init(void)
{
    P3SEL |= BIT3 | BIT4;
    UCA0CTL1 = UCSWRST | UCSSEL__SMCLK;
    UCA0CTL0 = 0;                    /* 8 data bits, no parity, one stop. */
    UCA0BR0 = 6;
    UCA0BR1 = 0;
    UCA0MCTL = UCOS16 | UCBRF_13 | UCBRS_2;
    UCA0CTL1 &= ~UCSWRST;
    UCA0IE = UCRXIE;
#if DEBUG_UART
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 = UCSWRST | UCSSEL__SMCLK;
    UCA1CTL0 = 0;
    UCA1BR0 = 6;
    UCA1BR1 = 0;
    UCA1MCTL = UCOS16 | UCBRF_13 | UCBRS_2;
    UCA1CTL1 &= ~UCSWRST;
#endif
}

/* Print bring-up observations without using the ESP32 serial link. */
static void debug_write(const char *text)
{
#if DEBUG_UART
    while (*text) {
        const uint32_t deadline = tick_now() + 3;
        while (!(UCA1IFG & UCTXIFG)) {
            if (due(tick_now(), deadline)) return;
        }
        UCA1TXBUF = (uint8_t)*text++;
    }
#else
    (void)text;
#endif
}

/* Timer1 task tick drives foreground work and low-power wakeups. */
static void scheduler_init(void)
{
    TA1CCR0 = 327;                    /* 328 / 32768 Hz = 10.0098 ms. */
    TA1CCTL0 = CCIE;
    TA1CTL = TASSEL__ACLK | MC__UP | TACLR;
}

/* Report activity with the two fitted Booster LEDs. */
static void indicate(bool success)
{
    P8OUT &= ~(LED_OK | LED_WARN);
    P8OUT |= success ? LED_OK : LED_WARN;
    led_until = tick_now() + 120;
}

/* Queue one typed command. Sequence zero is reserved for ESP32 UI frames. */
static bool enqueue(CommandKind kind, const char *arguments)
{
    Command *command;
    if (command_count >= COMMAND_CAPACITY || strlen(arguments) >= sizeof commands[0].arguments) return false;
    if (++next_sequence == 0) ++next_sequence;
    command = &commands[(command_head + command_count) % COMMAND_CAPACITY];
    command->kind = kind;
    command->seq = next_sequence;
    command->queued_tick = tick_now();
    strcpy(command->arguments, arguments);
    ++command_count;
    return true;
}

/* Remove the oldest command once acknowledged or timed out. */
static void finish_command(void)
{
    command_head = (command_head + 1U) % COMMAND_CAPACITY;
    --command_count;
    attempts = 0;
    waiting_ack = false;
}

/* Drop only MED lookups, preserving other queued reader/card commands.
 * TX ISR owns its independent byte frame; an already-started old request may
 * finish transmitting, but its reply is ignored after this context is cleared. */
static void cancel_refill_lookup(void)
{
    uint8_t input, kept = 0;
    const uint8_t original = command_count;
    const bool removed_head = original && commands[command_head].kind == CMD_MED;
    for (input = 0; input < original; ++input) {
        const uint8_t from = (command_head + input) % COMMAND_CAPACITY;
        const uint8_t to = (command_head + kept) % COMMAND_CAPACITY;
        if (commands[from].kind == CMD_MED) continue;
        if (from != to) commands[to] = commands[from];
        ++kept;
    }
    command_count = kept;
    if (removed_head) { waiting_ack = false; attempts = 0; }
    refill_fetch_active = refill_request_pending = refill_view_active = false;
    showing_saved_copy = false;
    refill_next_day = refill_page_day = refill_page_chunk = 0;
    memset(refill_text, 0, sizeof refill_text);
}

/* Begin a new seven-day fetch. Keep the last complete snapshot available on failure. */
static void start_refill_lookup(void)
{
    cancel_refill_lookup();
    refill_fetch_active = true;
    refill_next_day = 1;
}

/* Display the last complete snapshot; reason is the short LCD explanation if none exists. */
static void show_cached(const char *reason)
{
    cancel_refill_lookup();
    if (!cache_valid) {
        display("No saved info", reason, 250);
        indicate(false);
        return;
    }
    memcpy(refill_text, cached_text, sizeof refill_text);
    refill_view_active = true;
    showing_saved_copy = true;
    refill_page_day = refill_page_chunk = 0;
    refill_hold_until = tick_now() + REFILL_HOLD_TICKS;
    display(cached_week, "Saved copy", 250);
    indicate(true);
    debug_write("NFC: live lookup unavailable, showing saved copy.\r\n");
}

/* Queue one MED request at a time. The caller passes its tick; replies advance the day. */
static void service_refill(uint32_t now)
{
    char day[3], second[17];
    (void)now;
    if (!refill_fetch_active || refill_request_pending) return;
    if (refill_next_day < 1 || refill_next_day > 7) { cancel_refill_lookup(); return; }
    snprintf(day, sizeof day, "%u", refill_next_day);
    if (!enqueue(CMD_MED, day)) return;
    refill_request_pending = true;
    snprintf(second, sizeof second, "Day %u of 7", refill_next_day);
    display("Reading refill", second, 100);
}

/* Accept only protocol-defined pillbox state names before copying text to box_state. */
static bool box_state_valid(const char *text)
{
    static const char *const states[] = {
        "CLOCK", "SETUP", "TEST", "TEST_OVERDUE", "PAUSED", "NOT_DUE", "READY",
        "ATTEMPTED", "COMPLETED", "MISSED", "FAULT", "POSITIONING"
    };
    unsigned i;
    for (i = 0; i < sizeof states / sizeof states[0]; ++i)
        if (!strcmp(text, states[i])) return true;
    return false;
}

/* Return whether the last environment frame is still current at foreground tick now. */
static bool environment_fresh(uint32_t now)
{
    return remote_environment_seen && !due(now, remote_environment_tick + REMOTE_FRESH_TICKS);
}

/* Return whether the last schedule/status frame is still current at foreground tick now. */
static bool box_fresh(uint32_t now)
{
    return remote_box_seen && !due(now, remote_box_tick + REMOTE_FRESH_TICKS);
}

/* Remote sensor values are accepted only with range/sentinel/flags consistency. */
static void receive_environment(char **fields)
{
    int32_t t, rh;
    uint16_t f;
    bool tv, hv;
    char debug_line[80];
    if (!pv_i32(fields[3], &t) || !pv_i32(fields[4], &rh) || !pv_u16(fields[5], &f) || f > 7) return;
    tv = t >= -4000L && t <= 12500L;
    hv = rh >= 0 && rh <= 10000;
    if ((!tv && t != ADC_TEMP_INVALID) || (!hv && rh != DHT_HUMIDITY_INVALID) || ((!tv || !hv) && !(f & 4))) return;
    temperature_x100 = t;
    humidity_x100 = (uint16_t)rh;
    flags = (uint8_t)f;
    remote_environment_tick = tick_now();
    remote_environment_seen = true;
    snprintf(debug_line, sizeof debug_line, "PB_ENV temp_x100=%ld,rh_x100=%u,flags=%u\r\n", (long)t, humidity_x100, flags);
    debug_write(debug_line);
}

/* Validate the split BOX frame's day, state and flags before refreshing its timestamp. */
static void receive_box(char **fields)
{
    uint16_t day, overdue, refill;
    char debug_line[72];
    if (!pv_u16(fields[3], &day) || day > 7 || !box_state_valid(fields[4]) ||
        !pv_u16(fields[5], &overdue) || overdue > 1 || !pv_u16(fields[6], &refill) || refill > 1) return;
    box_day = (uint8_t)day;
    strcpy(box_state, fields[4]);
    box_overdue = overdue != 0;
    box_refill = refill != 0;
    remote_box_tick = tick_now();
    remote_box_seen = true;
    snprintf(debug_line, sizeof debug_line, "PB_BOX day=%u,state=%s,overdue=%u,refill=%u\r\n", day, box_state, overdue, refill);
    debug_write(debug_line);
}


/* A card lookup waiting behind an unavailable link must not appear much later.
 * Retries of an already sent TAG keep their sequence and use ESP replay handling.
 */
static bool expire_queued_tag(uint32_t now)
{
    char uid[21];
    if (!command_count || attempts || commands[command_head].kind != CMD_TAG ||
        !due(now, commands[command_head].queued_tick + TAG_QUEUE_MAX_AGE)) return false;
    strcpy(uid, commands[command_head].arguments);
    finish_command();
    if (command_count && commands[command_head].kind == CMD_LOGREQ &&
        !strcmp(commands[command_head].arguments, uid)) finish_command();
    debug_write("NFC tap expired before transmission.\r\n");
    show_cached("Link busy");
    return true;
}

/* Encode and queue bytes to the TX ISR. Retries keep the original sequence. */
static void service_transmit(uint32_t now)
{
    static const char *const names[] = {"ENV", "TAG", "LOGREQ", "CLEAR", "READER", "MED"};
    const Command *command;
    char body[PV_FRAME_SIZE];
    size_t length;
    int written;
    if (!command_count || (UCA0IE & UCTXIE)) return;
    if (expire_queued_tag(now)) return;
    if (waiting_ack && !due(now, sent_tick + ACK_TIMEOUT)) return;
    command = &commands[command_head];
    if (attempts >= 3) {
        if (command->kind == CMD_MED) { show_cached("No reply"); return; }
        if (command->kind == CMD_TAG) { finish_command(); show_cached("No reply"); return; }
        display("ESP32 link error", "No reply", 250);
        indicate(false);
        finish_command();
        return;
    }
    written = snprintf(body, sizeof body, "PV1,%u,%s%s%s", command->seq,
                       names[command->kind], command->arguments[0] ? "," : "",
                       command->arguments);
    length = written > 0 && written < (int)sizeof body ? pv_encode(body, tx_frame, sizeof tx_frame) : 0;
    if (!length) { finish_command(); return; }
    sent_tick = now;
    ++attempts;
    waiting_ack = true;
    tx_index = 0;
    tx_length = (uint8_t)length;
    /* UCA0IV cleared TXIFG when the previous frame's final empty-buffer
     * interrupt disabled TXIE. Re-arm that flag to start this frame too. */
    UCA0IFG |= UCTXIFG;
    UCA0IE |= UCTXIE;
}

/* Show the ESP32 state as a local prompt. Slot -1 means general device state. */
static void display_ui(const char *state, int32_t slot)
{
    static char previous[24];
    static int32_t previous_slot = -2;
    char second[17];
    /* Card information must survive routine ESP status traffic for about 8 seconds.
     * Fault messages and local sensor warnings may interrupt this informational view. */
    if ((refill_fetch_active || (refill_view_active && !due(tick_now(), refill_hold_until))) && !strstr(state, "FAULT")) return;
    if (strstr(state, "FAULT")) cancel_refill_lookup();
    refill_view_active = false;
    /* Keep stable status packets from hiding the idle environment view. */
    if (slot == previous_slot && !strcmp(state, previous)) return;
    snprintf(previous, sizeof previous, "%.23s", state);
    previous_slot = slot;
    if (slot >= 0) snprintf(second, sizeof second, "Compartment %ld", (long)slot + 1);
    else strcpy(second, "Device status");

    if (!strcmp(state, "DOSE_READY")) {
        display("Dose ready", second, 180);
    } else if (!strcmp(state, "DOSE_MOVING")) {
        display("Dispensing steps", second, 300);
    } else if (!strcmp(state, "DOSE_STEPS_DONE")) {
        display("Steps completed", second, 300);
    } else if (!strcmp(state, "MOTOR_FAULT")) {
        display("Motor fault", "Inspect carousel", 400);
    } else {
        display(state, second, 250);
    }
}

/* Decode up to48 printable medicine-instruction characters from uppercase hex. */
static bool decode_refill_label(const char *hex, char out[49])
{
    const size_t length = strlen(hex);
    size_t i;
    bool visible = false;
    if (!length || length > 96U || (length & 1U)) return false;
    for (i = 0; i < length; i += 2U) {
        const int hi = pv_hex(hex[i]), lo = pv_hex(hex[i + 1U]);
        unsigned value;
        if (hi < 0 || lo < 0) return false;
        value = ((unsigned)hi << 4) | (unsigned)lo;
        if (value < 32U || value > 126U || value == '"' || value == '\\') return false;
        out[i / 2U] = (char)value;
        if (value != ' ') visible = true;
    }
    out[length / 2U] = '\0';
    return visible;
}

/* Handle one validated inbound frame from the ESP32. */
static void receive_frame(char *line)
{
    char *fields[8];
    const size_t count = pv_split(line, fields, 8);
    uint16_t seq;
    int32_t slot;
    const Command *command;
    if (count < 3 || !pv_u16(fields[1], &seq)) return;
    if (seq == 0) {
        if (count == 6 && !strcmp(fields[2], "ENV")) receive_environment(fields);
        else if (count == 7 && !strcmp(fields[2], "BOX")) receive_box(fields);
        else if (count == 5 && !strcmp(fields[2], "UI") && pv_i32(fields[4], &slot) && slot >= -1 && slot < 8)
            display_ui(fields[3], slot);
        return;
    }
    if (!command_count || !waiting_ack || seq != commands[command_head].seq) return;
    command = &commands[command_head];
    if (count == 5 && !strcmp(fields[2], "ACK")) {
        const bool ok = !strcmp(fields[3], "OK");
        if (!ok && strcmp(fields[3], "ERR")) return;
        if (command->kind == CMD_MED) { show_cached("Refill error"); return; }
        if (command->kind == CMD_TAG) {
            display(ok ? "Card recognized" : "Request rejected", fields[4], 260);
            indicate(ok);
        }
        finish_command();
        return;
    }
    if (count == 5 && command->kind == CMD_TAG && !strcmp(fields[2], "WEEK") &&
        pv_i32(fields[3], &slot) && slot >= 0 && slot <= 7) {
        char first[17], second[17], debug_line[48];
        if (!strcmp(fields[4], "REFILL")) { strcpy(first, "Refill all 7days"); strcpy(second, "Reading list"); }
        else if (!strcmp(fields[4], "ACTIVE")) {
            if (!slot) strcpy(first, "Week starts soon");
            else snprintf(first, sizeof first, "Week day %ld of 7", (long)slot);
            strcpy(second, "Reading list");
        } else if (!strcmp(fields[4], "SETUP")) { strcpy(first, "Load 7 days"); strcpy(second, "Reading list"); }
        else if (!strcmp(fields[4], "CLOCK")) { strcpy(first, "Set clock in web"); strcpy(second, "Reading list"); }
        else return;
        display(first, second, 200);
        indicate(true);
        strcpy(pending_week, first); /* Publish this heading only after all seven matching replies. */
        snprintf(debug_line, sizeof debug_line, "Week day %ld: %s\r\n", (long)slot, fields[4]);
        debug_write(debug_line);
        finish_command();
        start_refill_lookup();
        return;
    }
    if (count == 5 && command->kind == CMD_MED && !strcmp(fields[2], "MED") &&
        refill_fetch_active && refill_request_pending && pv_i32(fields[3], &slot) && slot >= 1 && slot <= 7) {
        uint16_t requested;
        char decoded[49], debug_line[72];
        if (!pv_u16(command->arguments, &requested) || requested != (uint16_t)slot || requested != refill_next_day ||
            !decode_refill_label(fields[4], decoded)) return;
        strcpy(refill_text[slot - 1], decoded);
        snprintf(debug_line, sizeof debug_line, "Refill day %ld: %s\r\n", (long)slot, decoded);
        debug_write(debug_line);
        finish_command();
        refill_request_pending = false;
        if (slot < 7) ++refill_next_day;
        else {
            strcpy(cached_week, pending_week);
            memcpy(cached_text, refill_text, sizeof cached_text);
            cache_valid = true;
            refill_fetch_active = false;
            refill_view_active = true;
            refill_page_day = refill_page_chunk = 0;
            refill_hold_until = tick_now() + REFILL_HOLD_TICKS;
            lcd_hold_until = tick_now();
            indicate(true);
        }
    }
}

/* Drain the UART ring into LF-delimited frames. Reject corrupt/oversize lines. */
static void service_receive(void)
{
    static char line[PV_FRAME_SIZE];
    static uint8_t used;
    static bool dropping;
    if (rx_fault) {
        __disable_interrupt();
        rx_fault = false;
        __enable_interrupt();
        dropping = true;
    }
    while (rx_tail != rx_head) {
        const uint8_t ch = rx_ring[rx_tail++];
        if (ch == '\n') {
            if (!dropping && used) { line[used] = '\0'; receive_frame(line); }
            used = 0;
            dropping = false;
        } else if (ch != '\r') {
            if (ch < 32 || ch > 126 || used >= sizeof line - 1) dropping = true;
            if (!dropping) line[used++] = (char)ch;
        }
    }
}

/* Convert a newly presented card to uppercase hex, then queue its commands. */
static void report_card(const uint8_t *uid, uint8_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[21];
    uint8_t i;
    if (length != 4 && length != 7 && length != 10) return;
    for (i = 0; i < length; ++i) {
        text[2U * i] = hex[uid[i] >> 4];
        text[2U * i + 1U] = hex[uid[i] & 0x0f];
    }
    text[2U * length] = '\0';
    if (!strcmp(held_uid, text)) return;
    debug_write("UID "); debug_write(text); debug_write("\r\n");
    cancel_refill_lookup(); /* Cancel the pending request; the previous complete snapshot survives. */
    /* Do not defer lookup after queue overflow. Resynchronize and require a new tap.
     * A replacement card sends CLEAR before TAG; no LOGREQ may overwrite its name. */
    if (reader_resync_pending || command_count > COMMAND_CAPACITY - (held_uid[0] ? 2U : 1U)) {
        strcpy(held_uid, text);
        reader_resync_pending = true;
        show_cached("Link busy");
        return;
    }
    if (held_uid[0]) enqueue(CMD_CLEAR, held_uid);
    strcpy(held_uid, text);
    if (remote_warning_mask & 8) { show_cached("Pillbox offline"); return; }  /* No live link: saved copy only. */
    enqueue(CMD_TAG, text);       /* WEEK reply starts sequential MED1..7 lookups. */
    if (length <= 7) display("Reading card UID", text, 180);
    else {
        char first[17];
        snprintf(first, sizeof first, "UID %.12s", text);
        display(first, text + 12, 180);
    }
}

/* Poll card presence. CLEAR and EMPTY are emitted only after one continuous
 * second without a detected card while the RC522 still passes register checks.
 * READER BOOT also marks a reader reconnect/desync and pauses ESP32 auto mode. */
static void service_reader(uint32_t now)
{
    uint8_t uid[10], length;
    if (!due(now, next_nfc)) return;
    if (!reader_online) {
        reader_online = rc522_init();
        next_nfc = tick_now() + (reader_online ? NFC_PERIOD : 500UL);
        if (reader_online) {
            reader_resync_pending = true;
            absence_pending = absence_confirmed = false;
            next_reader_heartbeat = now;
        }
        return;
    }
    next_nfc = now + NFC_PERIOD;
    if (reader_resync_pending && enqueue(CMD_READER, "BOOT")) {
        reader_resync_pending = false;
        next_reader_heartbeat = now;
    }
    if (rc522_scan(uid, &length)) {
        absence_pending = absence_confirmed = false;
        report_card(uid, length);
    } else {
        if (!rc522_healthy()) {
            reader_online = false;
            reader_resync_pending = true;
            absence_pending = absence_confirmed = false;
            if (enqueue(CMD_READER, "BOOT")) reader_resync_pending = false;
            cancel_refill_lookup();
            display("Reader offline", "Check RFID wiring", 250);
            return;
        }
        if (!rc522_no_card()) {
            /* RF collisions/partial reads still imply possible presence. */
            absence_pending = absence_confirmed = false;
        } else {
            if (!absence_pending) {absence_pending = true; absence_started = now;}
            if (!absence_confirmed && due(now, absence_started + CARD_REMOVE_TICKS)) {
                absence_confirmed = true;
                next_reader_heartbeat = now;
            }
            if (absence_confirmed && held_uid[0] && enqueue(CMD_CLEAR, held_uid)) {
                held_uid[0] = '\0';
                next_reader_heartbeat = now;
            }
        }
    }
    if (!reader_resync_pending && due(now, next_reader_heartbeat)) {
        if (enqueue(CMD_READER, absence_confirmed && !held_uid[0] ? "EMPTY" : "ONLINE")) {
            next_reader_heartbeat = now + READER_HEARTBEAT;
        }
    }
}

/* Both data streams have independent freshness; cached values are never live
 * merely because a card request was acknowledged. New warnings interrupt a
 * refill view once; a steady warning does not continually hide its pages. */
static void service_remote(uint32_t now)
{
    uint8_t warnings = 0;
    const bool env_fresh = environment_fresh(now);
    const bool status_fresh = box_fresh(now);
    if (!env_fresh) {
        temperature_x100 = ADC_TEMP_INVALID;
        humidity_x100 = DHT_HUMIDITY_INVALID;
        flags = 4;
    }
    if ((!env_fresh && (remote_environment_seen || now >= REMOTE_FRESH_TICKS)) ||
        (!status_fresh && (remote_box_seen || now >= REMOTE_FRESH_TICKS))) warnings |= 8;
    if (env_fresh) warnings |= flags;
    if (status_fresh && box_overdue) warnings |= 16;
    if (status_fresh && box_refill) warnings |= 32;
    if (status_fresh && (!strcmp(box_state, "FAULT") || !strcmp(box_state, "ATTEMPTED"))) warnings |= 64;
    if (status_fresh && !strcmp(box_state, "TEST_OVERDUE")) warnings |= 128;
    if ((warnings & 8) && !(remote_warning_mask & 8) && (refill_fetch_active || refill_view_active)) {
        show_cached("Pillbox offline");   /* Link lost mid-tap: keep showing information. */
    } else if ((warnings & (uint8_t)~remote_warning_mask) != 0) {
        cancel_refill_lookup();
        if (warnings & 8) display("PB data offline", "Check wireless", 300);
        else if (warnings & 4) display("PB sensor fault", "Check sensors", 300);
        else if (warnings & 3) display("Storage warning", "Check PB T / RH", 300);
        else if (warnings & 64) display("PB action fault", "Inspect pillbox", 300);
        else if (warnings & 128) display("TEST overdue", "Check dashboard", 300);
        else if (warnings & 16) display("Missed/overdue", "Check dashboard", 300);
        else display("Refill all 7days", "Tap card for list", 300);
        indicate(false);
    }
    if ((remote_warning_mask & 128) && !(warnings & 128)) lcd_hold_until = now;
    remote_warning_mask = warnings;
}


/* Restore the idle environment view after transient prompts expire. */
static void service_lcd(uint32_t now)
{
    char first[17], second[17];
    if (led_until && due(now, led_until)) {
        P8OUT &= ~(LED_OK | LED_WARN);
        led_until = 0;
    }
    if (remote_warning_mask) P8OUT |= LED_WARN;
    else if (!led_until) P8OUT &= ~LED_WARN;
    if (!due(now, lcd_hold_until)) return;
    if (refill_fetch_active) {
        snprintf(second, sizeof second, "Day %u of 7", refill_next_day);
        display("Reading refill", second, 100);
        return;
    }
    if (refill_view_active && !due(now, refill_hold_until) && refill_page_day < 7) {
        const size_t length = strlen(refill_text[refill_page_day]);
        const uint8_t pages = (uint8_t)((length + 15U) / 16U);
        snprintf(first, sizeof first, "Day %u %s", (unsigned)refill_page_day + 1U,
                 showing_saved_copy ? "saved" : "refill");
        snprintf(second, sizeof second, "%.16s", refill_text[refill_page_day] + (unsigned)refill_page_chunk * 16U);
        display(first, second, REFILL_PAGE_TICKS);
        if (++refill_page_chunk >= pages) {
            refill_page_chunk = 0;
            ++refill_page_day; /* Keep the last page protected for its full hold. */
        }
        return;
    }
    refill_view_active = false;
    idle_status_page = !idle_status_page;
    if (idle_status_page) {
        if (!box_fresh(now)) display("PB status stale", "Check wireless", 200);
        else if (!strcmp(box_state, "TEST_OVERDUE")) display("TEST overdue", "Check dashboard", 200);
        else if (!strcmp(box_state, "TEST")) display("Test mode active", "No daily count", 200);
        else if (box_refill) display("Refill all 7days", box_overdue ? "Overdue: see web" : "Tap card for list", 200);
        else if (box_overdue) display("Missed/overdue", "Check dashboard", 200);
        else if (!strcmp(box_state, "FAULT") || !strcmp(box_state, "ATTEMPTED")) display("PB action fault", "Inspect pillbox", 200);
        else {
            snprintf(first, sizeof first, "Pillbox day %u/7", box_day);
            display(first, box_state, 200);
        }
        return;
    }
    if (!environment_fresh(now)) {
        display("PB T/RH stale", "Check wireless", 200);
        return;
    }
    if (temperature_x100 == ADC_TEMP_INVALID) strcpy(first, "Temp unavailable");
    else {
        const long magnitude = temperature_x100 < 0 ? -temperature_x100 : temperature_x100;
        snprintf(first, sizeof first, "T %s%ld.%ld C %s", temperature_x100 < 0 ? "-" : "",
                 magnitude / 100L, magnitude % 100L / 10L, flags & 1 ? "!" : "");
    }
    if (humidity_x100 == DHT_HUMIDITY_INVALID) strcpy(second, "RH unavailable");
    else snprintf(second, sizeof second, "RH %u.%u%% %s", humidity_x100 / 100U,
                  humidity_x100 % 100U / 10U, flags & 2 ? "!" : "");
    display(first, second, 200);
}

/* Timer1 ISR: wake foreground jobs without local ADC or DHT sampling.
 * TI CCS uses the pragma form; TI's MSP430-GCC uses the attribute form. */
#if defined(__TI_COMPILER_VERSION__)
#pragma vector=TIMER1_A0_VECTOR
__interrupt void scheduler_isr(void)
#else
void __attribute__((interrupt(TIMER1_A0_VECTOR))) scheduler_isr(void)
#endif
{
    ++ticks;
    __bic_SR_register_on_exit(LPM0_bits);
}

/* UART ISR: receive into the ring, or advance the active TX frame. */
#if defined(__TI_COMPILER_VERSION__)
#pragma vector=USCI_A0_VECTOR
__interrupt void uart_isr(void)
#else
void __attribute__((interrupt(USCI_A0_VECTOR))) uart_isr(void)
#endif
{
    switch (__even_in_range(UCA0IV, USCI_UCTXIFG)) {
    case USCI_UCRXIFG: {
        const uint8_t errors = UCA0STAT & UCRXERR;
        const uint8_t byte = UCA0RXBUF;
        const uint8_t next = (uint8_t)(rx_head + 1U);
        if (errors || next == rx_tail) rx_fault = true;
        else { rx_ring[rx_head] = byte; rx_head = next; }
        __bic_SR_register_on_exit(LPM0_bits);
        break;
    }
    case USCI_UCTXIFG:
        if (tx_index < tx_length) UCA0TXBUF = (uint8_t)tx_frame[tx_index++];
        else UCA0IE &= ~UCTXIE;
        break;
    default:
        break;
    }
}

/* Initialize the board and service foreground jobs while IRQs own timing. */
int main(void)
{
    clocks_init();
    P8DIR |= LED_OK | LED_WARN;
    P8OUT &= ~(LED_OK | LED_WARN);
    lcd_init();
    uart_init();
    scheduler_init();
    __enable_interrupt();
    reader_online = rc522_init();
    next_nfc = tick_now() + (reader_online ? NFC_PERIOD : 500UL);
    /* Seed the sequence from the running timer to reduce reset collisions. */
    next_sequence = TA1R;
    enqueue(CMD_READER, "BOOT");
    next_reader_heartbeat = tick_now();
    display("PHARMAVAULT", reader_online ? "Tap a card" : "Check RFID wiring", 200);
    debug_write("PHARMAVAULT healthcare MSP430 ready (wireless provider 1)\r\n");
    debug_write("MSP firmware 2026-11-25: complete refill snapshots\r\n");
    for (;;) {
        uint32_t now = tick_now();
        service_receive();
        service_remote(now);
        service_reader(now);
        service_refill(now);
        service_transmit(tick_now());
        service_lcd(now);
        __disable_interrupt();
        if (rx_head == rx_tail && ticks == now) {
            __bis_SR_register(LPM0_bits | GIE);   /* Keep UART SMCLK available. */
        } else {
            __enable_interrupt();
        }
    }
}

/* Team record: add the actual contributors and version history at the top.
 * Each member adds their own 5–10 lines on implementation, evidence and learning.
 */
