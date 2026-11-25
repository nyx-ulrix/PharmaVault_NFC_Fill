/* Author: Mohamed Afiq (MA). RC522 NFC reader driver over SPI. Revised 2026-11-25. */
#ifndef PHARMAVAULT_RC522_H
#define PHARMAVAULT_RC522_H

#include <stdbool.h>
#include <stdint.h>

/* Configure UCB0 SPI for a 1.048576 MHz SMCLK and initialize the reader.
 * Returns false if the reader does not respond to its register checks.
 */
bool rc522_init(void);

/* Check SPI register access and the configured antenna/timer state. This
 * distinguishes an unplugged reader from an ordinary no-card scan. */
bool rc522_healthy(void);

/* True only if the last scan's WUPA ended in a clean hardware timer timeout
 * with no received bytes/errors. A collision/CRC/selection failure is not
 * evidence that a previously presented card was removed. */
bool rc522_no_card(void);

/* Poll one ISO/IEC 14443-A tag and copy its 4-, 7-, or 10-byte UID.
 * uid must point to a ten-byte buffer and uid_len to a length variable.
 * Returns false and sets uid_len to zero on no card, a collision, or an error.
 * Call periodically from foreground code. WUPA can see a card held in range.
 */
bool rc522_scan(uint8_t uid[10], uint8_t *uid_len);

#endif
