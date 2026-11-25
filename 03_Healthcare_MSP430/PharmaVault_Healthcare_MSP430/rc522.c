/* Author: Mohamed Afiq (MA). RC522 NFC reader driver over SPI. Revised 2026-11-25. */
/*
 * PHARMAVAULT RC522 reader driver, revision 1.0
 * Target MSP430F5529, TI CCS C99, MCLK 4.194304 MHz, SMCLK 1.048576 MHz.
 * UCB0 pins P3.0 (MOSI), P3.1 (MISO), P3.2 (SCK).
 * GPIO P2.3 is active-low NSS (labelled SDA on the RC522 breakout).
 * GPIO P1.6 drives the active-low RC522 reset. Power the breakout at 3.3 V.
 * Poll one ISO/IEC 14443-A tag at a time. This driver reads the public UID.
 * Register reference: https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf
 * Cascade selection: https://www.nxp.com/docs/en/application-note/AN10927.pdf
 */
#include "rc522.h"

#include <msp430.h>

enum {
    REG_COMMAND = 0x01,
    REG_COM_IRQ = 0x04,
    REG_ERROR = 0x06,
    REG_FIFO_DATA = 0x09,
    REG_FIFO_LEVEL = 0x0A,
    REG_CONTROL = 0x0C,
    REG_BIT_FRAMING = 0x0D,
    REG_COLL = 0x0E,
    REG_TX_MODE = 0x12,
    REG_RX_MODE = 0x13,
    REG_TX_CONTROL = 0x14,
    REG_TX_ASK = 0x15,
    REG_MOD_WIDTH = 0x24,
    REG_T_MODE = 0x2A,
    REG_T_PRESCALER = 0x2B,
    REG_T_RELOAD_H = 0x2C,
    REG_T_RELOAD_L = 0x2D,
    REG_VERSION = 0x37,
    COMMAND_IDLE = 0x00,
    COMMAND_TRANSCEIVE = 0x0C,
    IRQ_TIMER = 0x01,
    IRQ_ERROR = 0x02,
    IRQ_IDLE = 0x10,
    IRQ_RX = 0x20
};

static bool reader_ready;
static bool spi_failed;
static bool transceive_silent, scan_absent;

/* Transmit value on UCB0, returning the simultaneous receive byte. A timeout sets spi_failed. */
static uint8_t spi_transfer(uint8_t value)
{
    uint16_t remaining = 256;

    while ((UCB0IFG & UCTXIFG) == 0 && remaining != 0) {
        --remaining;
    }
    if ((UCB0IFG & UCTXIFG) == 0) {
        spi_failed = true;
        return 0;
    }

    UCB0TXBUF = value;
    remaining = 256;
    while ((UCB0IFG & UCRXIFG) == 0 && remaining != 0) {
        --remaining;
    }
    if ((UCB0IFG & UCRXIFG) == 0) {
        spi_failed = true;
        return 0;
    }
    return UCB0RXBUF;
}

/* Keep chip select low until the final SPI clock edge has completed. */
static void end_spi_frame(void)
{
    uint16_t remaining = 256;

    while ((UCB0STAT & UCBUSY) != 0 && remaining != 0) {
        --remaining;
    }
    if ((UCB0STAT & UCBUSY) != 0) {
        spi_failed = true;
    }
    P2OUT |= BIT3;
}

/* Write an MFRC522 register. The SPI address occupies bits 6 through 1. */
static void write_register(uint8_t reg, uint8_t value)
{
    if (spi_failed) {
        return;
    }
    P2OUT &= ~BIT3;
    (void)spi_transfer((uint8_t)(reg << 1));
    if (!spi_failed) {
        (void)spi_transfer(value);
    }
    end_spi_frame();
}

/* Read an MFRC522 register by setting bit 7 in the SPI address byte. */
static uint8_t read_register(uint8_t reg)
{
    uint8_t result = 0;

    if (!spi_failed) {
        P2OUT &= ~BIT3;
        (void)spi_transfer((uint8_t)(0x80u | (reg << 1)));
        if (!spi_failed) {
            result = spi_transfer(0);
        }
        end_spi_frame();
    }
    return result;
}

/* Compute ISO14443-A CRC from data[0..count-1]. Send low byte first. */
static uint16_t crc_a(const uint8_t *data, uint8_t count)
{
    uint16_t crc = 0x6363u;

    while (count != 0) {
        uint8_t x = (uint8_t)(*data++ ^ crc);
        x ^= (uint8_t)(x << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)x << 8) ^
                         ((uint16_t)x << 3) ^ ((uint16_t)x >> 4));
        --count;
    }
    return crc;
}

/* Send a frame and read an exact number of complete response bytes.
 * last_bits specifies the number of bits in the last TX byte (0 means 8).
 * rx_count = 0 is used for HALT, which expects silence until timeout.
 */
static bool transceive(const uint8_t *tx, uint8_t tx_count, uint8_t last_bits,
                       uint8_t *rx, uint8_t rx_count)
{
    uint8_t irq = 0;
    uint8_t i;
    bool ok = false;

    transceive_silent = false;

    write_register(REG_COMMAND, COMMAND_IDLE);
    write_register(REG_COM_IRQ, 0x7Fu);
    write_register(REG_FIFO_LEVEL, 0x80u);
    for (i = 0; i < tx_count; ++i) {
        write_register(REG_FIFO_DATA, tx[i]);
    }
    write_register(REG_BIT_FRAMING, last_bits);
    write_register(REG_COMMAND, COMMAND_TRANSCEIVE);
    write_register(REG_BIT_FRAMING, (uint8_t)(0x80u | last_bits));

    /* The hardware timer handles a missing tag. The iteration cap also
     * handles a malformed frame that starts but never finishes receiving.
     */
    for (i = 0; i < 100 && !spi_failed; ++i) {
        irq = read_register(REG_COM_IRQ);
        if (irq & (IRQ_RX | IRQ_IDLE | IRQ_TIMER | IRQ_ERROR)) {
            break;
        }
    }
    if (!spi_failed && (irq & IRQ_RX) != 0 &&
        (irq & (IRQ_TIMER | IRQ_ERROR)) == 0 && rx_count != 0 &&
        (read_register(REG_ERROR) & 0xDFu) == 0 &&
        read_register(REG_FIFO_LEVEL) == rx_count &&
        (read_register(REG_CONTROL) & 0x07u) == 0) {
        for (i = 0; i < rx_count; ++i) {
            rx[i] = read_register(REG_FIFO_DATA);
        }
        ok = !spi_failed;
    }
    /* Only a clean no-response timeout can later establish card absence.
     * A partial response, collision or selection error must not re-arm NFC. */
    transceive_silent = !spi_failed && (irq & IRQ_TIMER) != 0 &&
        (irq & (IRQ_RX | IRQ_ERROR)) == 0 &&
        (read_register(REG_ERROR) & 0xDFu) == 0 &&
        read_register(REG_FIFO_LEVEL) == 0 &&
        (read_register(REG_CONTROL) & 0x07u) == 0 && !spi_failed;
    write_register(REG_COMMAND, COMMAND_IDLE);
    write_register(REG_BIT_FRAMING, 0);
    return ok && !spi_failed;
}

/* Run anticollision and SELECT for one cascade level. The response is four
 * UID or cascade bytes plus BCC. Return the verified Select Acknowledge.
 */
static bool select_level(uint8_t level_code, uint8_t block[5], uint8_t *sak)
{
    uint8_t frame[9];
    uint8_t reply[3];
    uint8_t i;
    uint16_t crc;

    frame[0] = level_code;
    frame[1] = 0x20u;  /* Anticollision with no known UID bits. */
    if (!transceive(frame, 2, 0, block, 5) ||
        (uint8_t)(block[0] ^ block[1] ^ block[2] ^ block[3]) != block[4]) {
        return false;
    }

    frame[1] = 0x70u;  /* SELECT with the complete cascade block. */
    for (i = 0; i < 5; ++i) {
        frame[2 + i] = block[i];
    }
    crc = crc_a(frame, 7);
    frame[7] = (uint8_t)crc;
    frame[8] = (uint8_t)(crc >> 8);
    if (!transceive(frame, 9, 0, reply, 3)) {
        return false;
    }
    crc = crc_a(reply, 1);
    if (reply[1] != (uint8_t)crc || reply[2] != (uint8_t)(crc >> 8)) {
        return false;
    }
    *sak = reply[0];
    return true;
}

/* Put the selected tag in HALT. The next scan uses WUPA to find it again. */
static void halt_card(void)
{
    uint8_t frame[4] = {0x50u, 0, 0, 0};
    uint16_t crc = crc_a(frame, 2);

    frame[2] = (uint8_t)crc;
    frame[3] = (uint8_t)(crc >> 8);
    (void)transceive(frame, 4, 0, 0, 0);
}

/* Configure the reader's pins, SPI, RF timer and antenna. Return true after register readback. */
bool rc522_init(void)
{
    uint8_t version;

    reader_ready = false;
    spi_failed = false;

    /* Deassert CS before enabling SPI and hold the reader in reset. */
    P2SEL &= ~BIT3;
    P2OUT |= BIT3;
    P2DIR |= BIT3;
    P1SEL &= ~BIT6;
    P1OUT &= ~BIT6;
    P1DIR |= BIT6;

    UCB0CTL1 = UCSWRST | UCSSEL_2;
    UCB0CTL0 = UCCKPH | UCMSB | UCMST | UCSYNC;  /* Mode 0, 3-wire master. */
    UCB0BR0 = 10;  /* SMCLK / 10 is approximately 105 kHz. */
    UCB0BR1 = 0;
    UCB0IE = 0;
    P3SEL |= BIT0 | BIT1 | BIT2;
    UCB0CTL1 &= ~UCSWRST;

    __delay_cycles(8400);  /* 2 ms low reset pulse. */
    P1OUT |= BIT6;
    __delay_cycles(42000);  /* 10 ms for crystal startup. */

    version = read_register(REG_VERSION);
    if (spi_failed || version == 0 || version == 0xFFu) {
        return false;
    }
    write_register(REG_TX_MODE, 0);   /* 106 kbit/s, CRC handled in software. */
    write_register(REG_RX_MODE, 0);
    write_register(REG_MOD_WIDTH, 0x26u);
    write_register(REG_TX_ASK, 0x40u);
    write_register(REG_T_MODE, 0x80u);  /* Start timer after transmission. */
    write_register(REG_T_PRESCALER, 0xA9u);
    write_register(REG_T_RELOAD_H, 0);
    write_register(REG_T_RELOAD_L, 200);  /* 200 ticks at 40 kHz = 5 ms. */
    write_register(REG_TX_CONTROL, (uint8_t)(read_register(REG_TX_CONTROL) | 0x03u));
    __delay_cycles(21000);  /* 5 ms for a nearby tag to energize. */

    reader_ready = read_register(REG_T_PRESCALER) == 0xA9u &&
                   (read_register(REG_TX_CONTROL) & 0x03u) == 0x03u && !spi_failed;
    return reader_ready;
}

/* Check version and configured RF registers. Return false on missing hardware or SPI timeout. */
bool rc522_healthy(void)
{
    uint8_t version;
    if (!reader_ready) return false;
    spi_failed = false;
    version = read_register(REG_VERSION);
    return version != 0 && version != 0xFFu &&
           read_register(REG_T_PRESCALER) == 0xA9u &&
           (read_register(REG_TX_CONTROL) & 0x03u) == 0x03u && !spi_failed;
}

/* Report whether the last wakeup probe timed out cleanly. Collisions do not establish absence. */
bool rc522_no_card(void) {return scan_absent;}

/* Poll a card into the caller's ten-byte uid buffer. On success uid_len is 4, 7 or 10;
 * on failure it is zero. Only a completely selected card supplies a UID to the application. */
bool rc522_scan(uint8_t uid[10], uint8_t *uid_len)
{
    const uint8_t wakeup = 0x52u;
    const uint8_t cascade_codes[3] = {0x93u, 0x95u, 0x97u};
    uint8_t atqa[2];
    uint8_t block[5];
    uint8_t sak;
    uint8_t level;
    uint8_t length = 0;

    scan_absent = false;

    if (uid_len == 0) {
        return false;
    }
    *uid_len = 0;
    if (!reader_ready || uid == 0) {
        return false;
    }

    spi_failed = false;
    write_register(REG_COLL, (uint8_t)(read_register(REG_COLL) & 0x7Fu));
    if (!transceive(&wakeup, 1, 7, atqa, 2)) {
        scan_absent = transceive_silent && !spi_failed;
        return false;
    }

    for (level = 0; level < 3; ++level) {
        uint8_t i;
        if (!select_level(cascade_codes[level], block, &sak)) {
            return false;
        }
        if (sak & 0x04u) {
            /* CT is a cascade marker. Only the following three bytes are UID. */
            if (level == 2 || block[0] != 0x88u) {
                return false;
            }
            for (i = 1; i < 4; ++i) {
                uid[length++] = block[i];
            }
        } else {
            for (i = 0; i < 4; ++i) {
                uid[length++] = block[i];
            }
            halt_card();
            *uid_len = length;
            return true;
        }
    }
    return false;
}
