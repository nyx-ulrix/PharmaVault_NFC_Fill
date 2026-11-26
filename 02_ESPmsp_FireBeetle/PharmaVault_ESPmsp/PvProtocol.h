/* Author: Choo Zhen Da (CZD). PV1 frame protocol with CRC-16 (same design on all three boards). */
#ifndef PHARMAVAULT_PROTOCOL_H
#define PHARMAVAULT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PV_FRAME_SIZE 128u

/* CRC-16/CCITT-FALSE: initial 0xFFFF, polynomial 0x1021, no final XOR. */
static inline uint16_t pv_crc16(const char *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)(unsigned char)data[i] << 8);
        for (bit = 0; bit < 8; ++bit) {
            uint16_t shifted = (uint16_t)(crc << 1);
            crc = (crc & 0x8000u) ? (uint16_t)(shifted ^ 0x1021u) : shifted;
        }
    }
    return crc;
}

static inline int pv_body_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == ',' || c == '_' ||
           c == '-' || c == '+' || c == '.';
}

/* Copy a body to an output frame with checksum and LF. Return bytes to send. */
static inline size_t pv_encode(const char *body, char *out, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, length = strlen(body);
    uint16_t crc;
    if (length < 7 || length + 7 > capacity || length + 7 > PV_FRAME_SIZE) return 0;
    for (i = 0; i < length; ++i) if (!pv_body_char((unsigned char)body[i])) return 0;
    crc = pv_crc16(body, length);
    memcpy(out, body, length);
    out[length] = '*';
    for (i = 0; i < 4; ++i) out[length + 1 + i] = hex[(crc >> (12 - 4*i)) & 15u];
    out[length + 5] = '\n';
    out[length + 6] = '\0';
    return length + 6;
}

static inline int pv_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Validate a complete line without CR/LF, then split it in place. */
static inline size_t pv_split(char *line, char **fields, size_t capacity)
{
    size_t count = 1, i, body_length;
    char *p;
    char *star = strchr(line, '*');
    uint16_t checksum = 0;
    if (!star || strlen(star + 1) != 4 || capacity < 3) return 0;
    body_length = (size_t)(star - line);
    if (body_length < 7 || body_length + 7 > PV_FRAME_SIZE) return 0;
    for (i = 0; i < 4; ++i) {
        int nibble = pv_hex(star[i + 1]);
        if (nibble < 0) return 0;
        checksum = (uint16_t)((checksum << 4) | (unsigned)nibble);
    }
    if (checksum != pv_crc16(line, body_length)) return 0;
    for (p = line; p < star; ++p) if (!pv_body_char((unsigned char)*p)) return 0;
    *star = '\0';
    fields[0] = line;
    for (p = line; p < star; ++p) {
        if (*p != ',') continue;
        if (p == line || p[-1] == '\0' || p + 1 == star || count == capacity) return 0;
        *p = '\0';
        fields[count++] = p + 1;
    }
    if (count < 3 || strcmp(fields[0], "PV1") != 0) return 0;
    return count;
}

/* Parse decimal numbers without accepting trailing text or overflowing. */
static inline int pv_u16(const char *s, uint16_t *out)
{
    uint32_t value = 0;
    if (!s || !*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        value = value * 10u + (uint32_t)(*s++ - '0');
        if (value > 65535u) return 0;
    }
    *out = (uint16_t)value;
    return 1;
}

static inline int pv_i32(const char *s, int32_t *out)
{
    uint32_t value = 0;
    uint32_t limit;
    int negative;
    if (!s || !*s) return 0;
    negative = *s == '-';
    if (negative) ++s;
    if (!*s) return 0;
    limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*s) {
        uint32_t digit;
        if (*s < '0' || *s > '9') return 0;
        digit = (uint32_t)(*s++ - '0');
        if (value > (limit - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    if (negative && value == UINT32_C(2147483648)) *out = INT32_MIN;
    else *out = negative ? -(int32_t)value : (int32_t)value;
    return 1;
}

static inline int pv_uid_valid(const char *uid)
{
    size_t i, n = strlen(uid);
    if (n != 8 && n != 14 && n != 20) return 0;
    for (i = 0; i < n; ++i) if (pv_hex(uid[i]) < 0) return 0;
    return 1;
}

#endif
