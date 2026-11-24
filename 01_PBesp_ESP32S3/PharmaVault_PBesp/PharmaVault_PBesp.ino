/* PHARMAVAULT PBesp wireless 2; independent patient pillbox. ESP32-S3 / Arduino-ESP32 core 3.x.
 * AI-assisted prototype. Team: record actual authors, changes and personal reflections.
 * 28BYJ-48 via ULN2003 IN1/2/3/4 -> GPIO5/6/7/8; IR OUT -> GPIO14 at3.3V.
 * Local sensors: GPIO1 ADC1 NTC10k/B3950; GPIO2 DHT11 DATA, both 3V3.
 * No servo is used. GPIO3 and GPIO13 are unused inputs; each station has its own local common ground.
 * MSP430 link arrives over the paired FireBeetle Wi-Fi bridge; S3 GPIO4/17 unused.
 * 13:00 Singapore makes the next dose available WITHOUT motor movement.
 * A fresh continuous1.5s hand presentation advances one compartment to dispense.
 * 14:00 flags incomplete days overdue. One late attempt is permitted until midnight.
 * The attempt is saved before any coil energizes; uncertain attempts never auto-retry.
 * NFC displays weekly progress/refill status on the MSP LCD, without movement.
 * After ESP boot set laptop time, confirm actual CURRENT compartment and enable.
 * Position/pill delivery are not physically measured. Test mechanical release with dummy contents.
 * Earlier daily firmware Ready records are faulted when upgrading to the weekly workflow.
 * IR is LOW=hand, HIGH=clear; fixed1500ms. No website IR calibration.
 * Optional RAM-only test mode repeats fresh hand-triggered rotations with daily schedule paused.
 * Test rotations do not record daily doses; restart turns test mode off.
 * Seven date-specific days, slot8 empty parking; NFC only reports weekly status.
 * Timer repeats; refill reminder after day7 completion or day8 midnight.
 * Serial115200: ? access details, i diagnostics. Pair with healthcare MSP430 wireless provider1.
 */
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_random.h>

// These GPIO assignments target the ESP32-S3. D-number aliases vary by board.
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module. This sketch uses the S3 wiring map."
#endif

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

// Local authenticated bridge transport; all other operating logic is retained.
#include "PvWirelessSerial.h"
#include "PvPillboxSensors.h"
#include "PvMascot.h"
#include "PvDashboardGz.h"

static const char DashboardHtml[] PROGMEM = R"PVHTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light only">
<title>PharmaVault · Daily care</title>
<style>
/* Shapes: cards and tiles 28px, inputs 16px, buttons, badges and nav are full pills. */
:root{--bg:#e8f0e3;--bg2:#f4f8f1;--card:#fbfdf9;--ink:#17231b;--muted:#56665b;--line:#d8e3d2;--accent:#3d7a4c;--accent-soft:#d6ead9;--cta:#1d2a21;--cta-ink:#f4f8f1;--peach:#fbe2d3;--sky:#d9e9f1;--butter:#f5edc4;--warn-bg:#fbe4da;--warn-ink:#8a3a1f;--err-bg:#fbe0dc;--err-ink:#962c22;--ring:#c99a2e;--shadow:0 10px 30px rgb(61 104 70/.10)}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:var(--ink);background:linear-gradient(180deg,var(--bg) 0,var(--bg2) 420px) fixed;min-height:100dvh;-webkit-font-smoothing:antialiased}
h1,h2,h3,p{margin-top:0}
h1{font-size:clamp(2rem,5vw,2.9rem);font-weight:800;letter-spacing:-.04em;line-height:1.08;margin-bottom:10px}
h2{font-size:1.2rem;font-weight:750;letter-spacing:-.02em;margin-bottom:12px}
p{margin-bottom:14px}a{color:var(--accent)}
small,.quiet,.note{color:var(--muted);font-size:.84rem}small{display:block;line-height:1.6}
.eyebrow{font-size:.78rem;font-weight:650;color:var(--muted);margin-bottom:8px}
button,input,select{font:inherit;color:inherit}
button{border:0;border-radius:999px;background:var(--cta);color:var(--cta-ink);padding:12px 22px;font-size:.9rem;font-weight:700;cursor:pointer;min-height:46px;transition:transform .15s ease,opacity .15s ease}
button:active:not(:disabled){transform:scale(.97)}
button:disabled{opacity:.38;cursor:not-allowed}
.secondary{background:var(--accent-soft);color:var(--ink)}
.ghost{background:transparent;color:var(--ink);box-shadow:inset 0 0 0 1.5px var(--line)}
button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid var(--ring);outline-offset:3px}
.shell{max-width:1080px;padding:0 24px;margin:auto}
.topbar{padding-top:18px}
.toprow{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:64px}
.brand{display:flex;align-items:center;gap:12px;font-weight:800;font-size:1.1rem;letter-spacing:-.03em}
.brand small{font-size:.75rem;font-weight:500;letter-spacing:0;color:var(--muted)}
.brand-mark{display:grid;place-items:center;width:44px;height:44px;border-radius:999px;background:var(--cta);color:var(--cta-ink);font-size:.8rem;font-weight:800}
.connection{white-space:nowrap;display:flex;align-items:center;gap:8px;font-size:.78rem;font-weight:600;color:var(--muted);background:var(--card);border-radius:999px;padding:8px 14px;box-shadow:var(--shadow)}
.connection:before{content:"";width:8px;height:8px;border-radius:50%;background:#a3aaa0}
.connection[data-online=true]:before{background:#3fa35a}
.nav{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);z-index:10;display:flex;gap:6px;padding:6px;border-radius:999px;background:var(--cta);box-shadow:0 14px 40px rgb(20 40 26/.28)}
.nav button{background:transparent;color:var(--cta-ink);opacity:.7;min-width:128px;padding:12px 22px}
.nav button[aria-selected=true]{background:var(--cta-ink);color:var(--cta);opacity:1}
.top-status{display:flex;flex-wrap:wrap;justify-content:flex-end;align-items:center;gap:8px}.nav-note{white-space:nowrap;font-size:.78rem;font-weight:700;border-radius:999px;background:var(--warn-bg);color:var(--warn-ink);padding:8px 14px;box-shadow:var(--shadow)}
.workspace{padding-top:14px;padding-bottom:120px}
.global-notice:empty,.message:empty{display:none}
.global-notice{background:var(--accent-soft);color:var(--ink)}.global-notice,.alert{padding:13px 18px;border-radius:16px;margin-bottom:12px;font-size:.88rem}
.alert{background:var(--warn-bg);color:var(--warn-ink)}.error{background:var(--err-bg);color:var(--err-ink)}
.test-notice{display:flex;gap:16px;align-items:center;justify-content:space-between;background:var(--cta);color:var(--cta-ink);border-radius:28px;padding:14px 16px 14px 22px;margin-bottom:16px}
.test-notice p{margin:0;font-size:.86rem}.test-notice button{flex-shrink:0;background:var(--cta-ink);color:var(--cta)}
.intro{display:flex;justify-content:space-between;align-items:flex-end;gap:20px;margin:18px 0 24px}
.intro p{margin-bottom:0;color:var(--muted)}
.badge{display:inline-flex;align-items:center;gap:6px;border-radius:999px;padding:7px 13px;background:var(--accent-soft);font-size:.76rem;font-weight:700;white-space:nowrap;color:var(--ink)}
.panel{background:var(--card);border-radius:28px;padding:26px;min-width:0;box-shadow:var(--shadow)}
.hero{position:relative;overflow:hidden;background:linear-gradient(135deg,var(--accent-soft),var(--card) 75%);display:flex;flex-direction:column;gap:10px}
.hero>*{position:relative}.hero{min-height:340px}.mascot{position:absolute!important;right:34px;top:20px;height:232px;width:auto;pointer-events:none;filter:drop-shadow(0 10px 18px rgb(40 70 46/.18))}.hero-copy{max-width:calc(100% - 270px)}

.hero-title{font-size:clamp(1.9rem,4.4vw,3rem);font-weight:800;letter-spacing:-.045em;line-height:1.06;margin:4px 0 12px;max-width:16ch}
.hero p{max-width:46ch;color:var(--muted)}
.hero-bottom{margin-top:auto!important;display:flex;align-items:center;justify-content:space-between;gap:14px;padding-top:16px;margin-top:8px;border-top:1px solid var(--line)}
.sensor-label{font-size:.76rem;color:var(--muted)}.sensor-value{font-weight:750;font-size:1rem}
.cards{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.35fr);gap:16px;margin-top:16px}.env{padding:20px 22px}.env-pair{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:6px}.env-pair span{font-size:.78rem;opacity:.7}.env-pair strong{display:block;font-size:1.6rem;margin:2px 0 0}.env p{margin:8px 0 0;font-size:.78rem}
.stat{display:flex;flex-direction:column;gap:2px}
.stat strong{font-size:2.3rem;font-weight:800;letter-spacing:-.05em;line-height:1.1;margin:6px 0 2px}
.stat-label{font-size:.86rem;font-weight:700}
.stat .quiet{color:var(--ink);opacity:.62}
.tint-peach{background:var(--peach)}.tint-sky{background:var(--sky)}.tint-butter{background:var(--butter)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.split,.section-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}
.time-row,.field-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.time-row{margin-top:12px}.time-row span{font-size:.78rem;opacity:.7}
.time-row strong{display:block;font-size:1.6rem;font-weight:800;letter-spacing:-.04em}
.env-line{margin:12px 6px 0}
.full-row{margin-top:16px}
.grid{display:grid;grid-template-columns:minmax(0,1.55fr) minmax(260px,1fr);gap:16px}
.week-dots{display:grid;grid-template-columns:repeat(7,1fr);gap:10px;margin:8px 0 16px}
.week-dot{display:grid;place-items:center;aspect-ratio:1;max-width:58px;width:100%;justify-self:center;border-radius:999px;background:var(--bg);color:var(--muted);font-weight:750;font-size:.95rem}
.week-dot[data-done=true]{background:var(--accent);color:var(--cta-ink)}
.week-dot[data-missed=true]{background:var(--peach);color:var(--warn-ink)}
.week-dot[data-current=true]{box-shadow:0 0 0 3px var(--ring) inset}
#weekProgress{font-size:1rem;font-weight:650;color:var(--muted);margin:-6px 0 0}.field-grid label{margin:4px 0}
details{margin:14px 0 0;border-top:1px solid var(--line);padding-top:14px}
summary{font-size:.9rem;font-weight:700;cursor:pointer;list-style-position:outside}
details>p:first-of-type{margin-top:14px}details p,details small,details ol{font-size:.86rem}
ol{padding-left:22px;font-size:.9rem}li{margin:8px 0}
#dailyLog{list-style:none;padding:0;margin:16px 0 0;display:grid;gap:8px}
#dailyLog li{margin:0;padding:12px 16px;border-radius:16px;background:var(--bg2);font-size:.84rem;color:var(--muted)}
.step-list{list-style:none;padding:0;margin:18px 0;counter-reset:steps}
.step-list li{display:flex;gap:12px;align-items:flex-start;margin:14px 0;font-size:.9rem;counter-increment:steps}
.step-list li:before{content:counter(steps);display:grid;place-items:center;flex-shrink:0;width:28px;height:28px;border-radius:999px;background:var(--accent-soft);font-size:.76rem;font-weight:800}
.check{display:flex;align-items:flex-start;gap:10px;padding:13px 16px;border-radius:16px;background:var(--bg2);font-size:.86rem;margin:10px 0}
.check input{margin:3px 0 0;width:18px;height:18px;flex-shrink:0;accent-color:var(--accent)}
label{display:block;font-size:.86rem;font-weight:600;margin:14px 0 7px}
input,select{border:1.5px solid var(--line);border-radius:16px;background:var(--card);padding:11px 14px;max-width:100%;min-height:46px}
input:disabled,select:disabled{opacity:.55}
input[type=number]{width:120px}
input[type=text],input[type=password],input[type=time]{display:block;width:100%;margin-top:6px}
.message{font-size:.86rem;color:var(--ink);white-space:pre-wrap;padding:12px 16px;background:var(--accent-soft);border-radius:16px;margin:12px 0 0;overflow-wrap:anywhere}
.status{font-size:.92rem;overflow-wrap:anywhere}
#tag{overflow-wrap:anywhere;font:.84rem ui-monospace,monospace}
.footer{margin-top:28px;padding-top:16px;font-size:.76rem;color:var(--muted);display:flex;justify-content:space-between;gap:14px}
.text-link{background:none;padding:0;min-height:32px;font-size:.86rem;color:var(--accent);border-radius:0;text-decoration:underline;text-underline-offset:4px}
.section-anchor{scroll-margin-top:18px}
.jump{display:flex;flex-wrap:wrap;gap:8px;margin:-8px 0 22px}
.jump a{background:var(--card);border-radius:999px;padding:9px 16px;font-size:.84rem;font-weight:650;color:var(--ink);text-decoration:none;box-shadow:var(--shadow)}
.group-title{margin:34px 0 4px;font-size:1.4rem;font-weight:800;letter-spacing:-.03em}.group-note{margin-bottom:16px}
#alignmentSection{align-self:start}
@media(max-width:760px){.shell{padding-left:16px;padding-right:16px}.grid{grid-template-columns:1fr}.cards{grid-template-columns:1fr;gap:12px}.stat strong{font-size:1.9rem}.env-pair strong{font-size:1.5rem}.panel{padding:22px;border-radius:24px}.intro{flex-direction:column;align-items:flex-start;gap:10px;margin-top:10px}.nav button{min-width:118px}.footer{flex-direction:column;gap:3px}.brand small{display:none}.hero{min-height:0}.mascot{position:static!important;height:170px;align-self:center;margin:-4px 0 2px}.hero-copy{max-width:none}.week-dots{gap:6px}}
@media(max-width:440px){.toprow:has(.nav-note:not([hidden])) .brand>div{display:none}.nav-note,.connection{padding:8px 12px}}@media(prefers-reduced-motion:reduce){*{transition:none!important;scroll-behavior:auto!important}}
</style></head><body>
<header class="topbar"><div class="shell"><div class="toprow"><div class="brand"><span class="brand-mark" aria-hidden="true">PV</span><div>PharmaVault<small>A little care. Every day.</small></div></div><div class="top-status"><span id="refillDot" class="nav-note" hidden>Refill due</span><span id="testDot" class="nav-note" hidden>Test on</span><span id="connectionState" class="connection" role="status">Connecting</span></div></div>
</div></header>
<nav class="nav" role="tablist" aria-label="Dashboard views"><button id="navToday" role="tab" aria-controls="viewToday" aria-selected="true" tabindex="0">Dashboard</button><button id="navSettings" role="tab" aria-controls="viewSettings" aria-selected="false" tabindex="-1">Settings</button></nav>
<main class="shell workspace">
<div id="connectionNotice" class="global-notice error" role="alert" hidden></div><p id="bridgeStatus" class="global-notice" role="status">Checking healthcare-station link...</p><p id="motionNotice" class="global-notice" role="status" hidden></p><p id="storage" class="global-notice error" role="alert"></p>
<div id="globalTest" class="test-notice" hidden><p id="irTestBanner" role="alert" hidden></p><button id="quickStopTest" type="button" disabled>Stop test</button></div>
<p id="overdueAlert" class="alert" role="alert" hidden></p><p id="weekRefillAlert" class="alert" role="alert" hidden></p><p id="globalActionMessage" class="message" role="status"></p>

<div id="viewToday" role="tabpanel" aria-labelledby="navToday">
<div class="intro"><div><p class="eyebrow" id="overviewDate">YOUR DAILY OVERVIEW</p><h1>Welcome back.</h1><p>Here's your pillbox today.</p></div><span id="modeBadge" class="badge">Connecting</span></div>

<section id="todayPanel" class="section-anchor"><div class="panel hero"><img class="mascot" src="/mascot.webp" alt="" width="342" height="360"><div class="hero-copy"><span class="badge" id="todayMedicine">TODAY'S MEDICINE</span><h2 id="heroTitle" class="hero-title">Connecting to your dispenser.</h2><p id="todayDetail">Your current status will appear here.</p><button id="nextAction" type="button" disabled>View setup</button></div><div class="hero-bottom"><div><div class="sensor-label">Hand sensor</div><div class="sensor-value" id="irSignal">Unknown</div></div><span class="badge" id="todaySlot">-</span></div></div></section>

<div class="cards">
<section class="panel stat env tint-peach"><span class="stat-label">Inside the pillbox</span><div class="env-pair"><div><span>Temperature</span><strong id="temp">-</strong></div><div><span>Humidity</span><strong id="rh">-</strong></div></div><p id="environment" class="quiet" role="status">Waiting for sensor readings.</p></section>
<section class="panel stat timer tint-butter"><div class="split"><span class="stat-label">Daily timer</span><span id="scheduleBadge" class="badge">-</span></div><div class="time-row"><div><span>Dose ready</span><strong id="summaryDue">-</strong></div><div><span>Overdue at</span><strong id="summaryAlert">-</strong></div></div><div class="row" style="margin-top:16px"><button id="manageSchedule" class="ghost" type="button">Change times</button><button id="quickPause" class="secondary" type="button" disabled>Pause</button></div></section>
</div>


<section id="weekPanel" class="panel full-row section-anchor"><div class="section-head" style="margin-bottom:8px"><div><h2>This week</h2><p id="weekProgress">Not started</p></div><span id="weekShort" class="badge">-</span></div>
<div class="week-dots" id="weekDots" aria-label="Seven scheduled days"></div><p id="weekSummary" class="quiet" style="margin:0">Waiting for weekly status.</p><p id="weekDates" class="quiet" style="margin:6px 0 0"></p>
<details id="refillDetails"><summary>Refill the week</summary><p id="weekStatus" class="status" role="status">Waiting for weekly status.</p><ol class="step-list"><li><div><strong>Pause and clear.</strong><br>Remove old or missed medicine first.</div></li><li><div><strong>Park at empty compartment 8.</strong><br>Don't pass loaded compartments across the outlet.</div></li><li><div><strong>Fill compartments 1 to 7.</strong><br>Each day's medicine goes in its own compartment.</div></li></ol><label class="check"><input id="weekFilledConfirm" type="checkbox" disabled><span>Old medicine removed; compartments 1 to 7 hold the right day's medicine.</span></label><label class="check"><input id="weekParkedConfirm" type="checkbox" disabled><span>Empty compartment 8 is at the outlet.</span></label><button id="refillWeek" type="button" disabled style="margin-top:8px">Start the seven-day pack</button><p id="weekMessage" class="message" role="status"></p><p id="weekNextStart" class="quiet" style="margin:16px 0 0"></p></details>
<details><summary>Which compartment is which day</summary><ol id="weekCompartments"></ol></details>
<button id="openRefill" class="text-link" type="button" hidden>Refill the week</button></section>

<div class="grid full-row"><section id="refillInstructionsPanel" class="panel"><h2>Refill notes</h2><p class="quiet">A short note per day. The healthcare station shows these on its LCD when the card is tapped.</p><form id="refillInstructionForm"><div class="field-grid"><label>Day 1<input id="instructionDay1" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 2<input id="instructionDay2" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 3<input id="instructionDay3" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 4<input id="instructionDay4" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 5<input id="instructionDay5" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 6<input id="instructionDay6" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 7<input id="instructionDay7" type="text" maxlength="48" autocomplete="off" disabled></label></div><small>Up to 48 characters each, no double quotes or backslashes. Blank shows "Not set".</small><p id="instructionsStatus" class="quiet" role="status"></p><button id="saveInstructions" type="submit" disabled>Save notes</button></form><p id="instructionsMessage" class="message" role="status"></p></section>
<section class="panel"><h2>NFC card</h2><p class="quiet">Tap the pillbox's card at the healthcare station to see this week's status and notes. A tap never dispenses medicine.</p><small>Last card</small><p id="tag">No card scanned</p><p id="nfcStatus" class="quiet" style="margin-bottom:0"></p></section></div>

<section class="panel full-row"><div class="section-head"><h2>Dose history</h2><span class="quiet">Last eight days</span></div><p id="alertSummary" class="alert" role="alert" hidden style="margin-top:14px"></p><details><summary>Show history and today's details</summary><p id="todayStatus" class="status"></p><p id="lastAction" class="quiet"></p><p id="doseWindow" class="quiet"></p><ol id="dailyLog"></ol><small>"Completed" means the motor finished turning. Taking the pills is not measured.</small></details></section>
</div>

<div id="viewSettings" role="tabpanel" aria-labelledby="navSettings" hidden>
<div class="intro"><div><h1>Settings</h1><p>Pause the schedule before changing settings or handling the carousel.</p></div><span class="badge">Singapore, UTC+8</span></div>
<nav class="jump" aria-label="Settings sections"><a href="#scheduleSection">Clock &amp; timer</a><a href="#alignmentSection">Carousel</a><a href="#irTestPanel">Test mode</a><a href="#wifiSection">Wi-Fi</a></nav>

<div class="grid"><section class="panel section-anchor" id="scheduleSection"><h2>Clock &amp; daily timer</h2><p id="clockStatus" class="status" role="status"></p><button id="syncClock" type="button" class="ghost" disabled>Sync clock from this device</button><p id="clockMessage" class="message" role="status"></p><div class="field-grid"><label>Dose ready at<input id="scheduleTime" type="time" value="13:00" required></label><label>Overdue alert at<input id="deadlineTime" type="time" value="14:00" required></label></div><small>The alert must be later the same day. Times repeat every day.</small><label class="check"><input id="scheduleConfirm" type="checkbox"><span>I checked the date, times, carousel position and hand sensor.</span></label><div class="row" style="margin-top:14px"><button id="saveSchedule" type="button" class="secondary" disabled>Save times</button><button id="enableSchedule" type="button" disabled>Enable schedule</button><button id="pauseSchedule" type="button" class="ghost" disabled>Pause schedule</button></div><p id="scheduleStatus" class="quiet" style="margin-top:16px"></p><p id="scheduleMessage" class="message" role="status"></p><details><summary>After a restart or a missed day</summary><p>After a restart, sync the clock, confirm the carousel position, then enable. Times and past doses stay saved.</p><p>If a day was missed, remove its medicine before aligning the compartment before today's. Don't rotate past a loaded missed compartment.</p><p>At the dose time the dose becomes ready without moving. A fresh hand turns the motor. An overdue dose stays available until midnight. A failed attempt never retries by itself.</p></details></section>

<section class="panel section-anchor" id="alignmentSection"><h2>Carousel</h2><details id="alignmentDetails"><summary>Confirm which compartment is at the outlet</summary><p id="alignmentStatus" class="status" role="status"></p><label>Compartment at the outlet now<select id="alignedSlot"><option value="">Choose after checking</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label><label class="check"><input id="alignmentConfirm" type="checkbox"><span>I removed any missed medicine and checked the position, without moving loaded missed compartments past the outlet.</span></label><button id="confirmAlignment" type="button" disabled>Confirm position</button><p id="alignmentMessage" class="message" role="status"></p><small>Before day N, align compartment N−1 (empty 8 before day 1). This does not move the motor.</small></details>
<details id="motorDetails"><summary>Motor steps and manual jog</summary><p>Use an empty carousel. Saving or jogging cancels a pending dose and needs the position confirmed again.</p><label>Half-steps per compartment <input id="stepsPerSlot" type="number" min="1" max="4096" step="1" value="512"></label><label>Direction <select id="stepDirection"><option value="1">Forward</option><option value="-1">Reverse</option></select></label><label class="check"><input id="stepperConfirm" type="checkbox"><span>Carousel empty; step count and direction checked.</span></label><button id="saveStepper" type="button" disabled>Save motor settings</button><small style="margin-top:12px">512 is the starting value for eight compartments.</small><label>Jog half-steps <input id="jogSteps" type="number" min="1" max="512" step="1" value="16"></label><label class="check"><input id="jogConfirm" type="checkbox"><span>The carousel is empty and clear; a jog can release anything left inside.</span></label><div class="row"><button id="jogBack" type="button" class="secondary" disabled>Jog backward</button><button id="jogForward" type="button" class="secondary" disabled>Jog forward</button></div><p id="stepperMessage" class="message" role="status"></p><p class="quiet" style="margin-top:14px">ULN2003 IN1-4 → GPIO5, 6, 7, 8. Driver on 5 V and common ground.</p></details></section></div>

<h2 class="group-title">Test mode</h2><p class="quiet group-note">Use an empty carousel or dummy contents. Starting a test pauses the daily schedule.</p>
<div class="grid"><section id="irTestPanel" class="panel section-anchor"><div class="split"><h2>Hand and motor test</h2><span class="badge"><span id="irTestCount">0</span>&nbsp;cycles</span></div><p id="irTestStatus" class="status" role="status"></p><p id="irTestGesture" class="quiet" role="status"></p><label class="check"><input id="irTestConfirm" type="checkbox"><span>The carousel is empty or holds dummy contents, and nothing is in the way.</span></label><div class="row" style="margin-top:16px"><button id="startIrTest" type="button" disabled>Start test mode</button><button id="stopIrTest" type="button" class="secondary" disabled>Stop test mode</button></div><p id="irTestMessage" class="message" role="status"></p><details><summary>How to test</summary><ol class="step-list"><li><div><strong>Keep clear for 1 second.</strong></div></li><li><div><strong>Hold a hand for 1.5 seconds.</strong><br>The carousel turns one compartment.</div></li><li><div><strong>Remove your hand.</strong><br>Wait 3 seconds, keep clear for 1 second, then repeat.</div></li></ol><small>Stop test mode and confirm the position before enabling the daily schedule. Closing this page does not stop test mode.</small></details></section>
<section id="timerTestPanel" class="panel"><h2>Timer and overdue test</h2><p class="quiet">A quick run of the daily timing: ready after 10 s, overdue after 30 s. It doesn't change saved times or the week. Tick the confirmation in the hand test first.</p><div class="field-grid"><p>Ready in<br><strong id="timerTestReady">Not started</strong></p><p>Overdue in<br><strong id="timerTestDeadline">Not started</strong></p></div><p id="timerTestStatus" class="status" role="status"></p><p id="timerTestAlert" class="overdue" role="alert" hidden></p><button id="startTimerTest" type="button" disabled>Start timer test</button><p id="timerTestMessage" class="message" role="status"></p><small>Use Stop test mode to finish. Test results are kept apart from the daily history.</small></section></div>
<section id="irDetails" class="panel full-row"><h2>Hand sensor</h2><p id="irStatus" class="quiet" role="status"></p><details><summary>Sensor diagnostics and wiring</summary><p id="irDiagnostic" class="quiet"></p><p>IR VCC → 3.3 V, GND → ground, OUT → GPIO14. LOW means hand, HIGH means clear.</p><small>If it always detects a hand, turn the sensitivity screw or re-aim the sensor.</small></details></section>

<section id="wifiSection" class="panel full-row section-anchor"><h2>Wi-Fi</h2><p id="networkStatus" class="quiet"></p><details><summary>Join another network</summary><form id="wifiForm"><label>Wi-Fi name<input id="ssid" type="text" maxlength="32" autocomplete="off" required></label><label>Password<input id="wifiPassword" type="password" maxlength="63" autocomplete="new-password"></label><small>2.4 GHz home network. The PharmaVault Wi-Fi stays on as well.</small><button id="joinWifi" disabled style="margin-top:14px">Save &amp; connect</button></form><p id="wifiMessage" class="message" role="status"></p></details></section>
</div>
<footer class="footer"><span>PharmaVault · Seven days of care</span><span>Motor moves are logged. Pill pickup is not measured.</span></footer>
</main><script>
'use strict';
const el = id => document.getElementById(id);
let state = null, token = '', busy = false, firstState = true, connected = false;
let irDisplayTimer = null, connectionFailed = false;
let instructionsDirty=false;
const irDisplayMaxAgeMs=2000;
const confirmations = ['scheduleConfirm','alignmentConfirm','stepperConfirm','jogConfirm','irTestConfirm','weekFilledConfirm','weekParkedConfirm'];
function clearConfirmations() { confirmations.forEach(id => {el(id).checked=false;}); }
function integer(value,min,max) {const n=Number(value);return Number.isInteger(n)&&n>=min&&n<=max?n:null;}
function minuteValue(value) {
  if(!/^\d{2}:\d{2}$/.test(value))return null;
  const [h,m]=value.split(':').map(Number);return h<=23&&m<=59?h*60+m:null;
}
function timeInput(minute) {return Number.isInteger(minute)&&minute>=0&&minute<=1439?String(Math.floor(minute/60)).padStart(2,'0')+':'+String(minute%60).padStart(2,'0'):'';}
function clockText(epoch) {
  if(!Number.isFinite(epoch)||epoch<=0)return 'Clock not set';
  return new Intl.DateTimeFormat('en-SG',{timeZone:'Asia/Singapore',year:'numeric',month:'short',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false}).format(new Date(epoch*1000))+' SGT';
}
function dayText(day) {return Number.isInteger(day)&&day>0?new Date(day*86400000).toISOString().slice(0,10):'Date unknown';}
function controls() {
  const available=connected&&Boolean(token)&&!busy;
  const idle=available&&!state?.motionActive;
  const setup=idle&&state?.storageOk&&!state?.scheduleEnabled&&!state?.irTestMode;
  const due=minuteValue(el('scheduleTime').value),deadline=minuteValue(el('deadlineTime').value);
  const timesValid=due!==null&&deadline!==null&&due<deadline;
  const timesSaved=timesValid&&due===state?.dueMinute&&deadline===state?.alertMinute;
  el('startIrTest').disabled=!idle||!state?.storageOk||!state?.canEnableIrTest||Boolean(state?.irTestMode)||!el('irTestConfirm').checked;
  el('stopIrTest').disabled=!idle||!state?.irTestMode;
  el('irTestConfirm').disabled=(Boolean(state?.irTestMode)&&state?.timerTestMode!==true)||busy;
  el('startTimerTest').disabled=!timerTestFieldsValid()||!idle||!state?.storageOk||!state?.stepperVerified||!state?.canTest||
    (Boolean(state?.irTestMode)&&state?.timerTestMode!==true)||!el('irTestConfirm').checked;
  el('syncClock').disabled=!setup;
  el('saveSchedule').disabled=!setup||!timesValid||!el('scheduleConfirm').checked;
  el('enableSchedule').disabled=!setup||!state?.canEnableSchedule||!timesSaved||!el('scheduleConfirm').checked;
  el('pauseSchedule').disabled=!idle||!state?.scheduleEnabled;
  el('scheduleTime').disabled=Boolean(state?.scheduleEnabled)||Boolean(state?.irTestMode)||busy;
  el('deadlineTime').disabled=Boolean(state?.scheduleEnabled)||Boolean(state?.irTestMode)||busy;
  const pendingTarget=state?.today?.status==='READY'?integer(state?.today?.slot,1,8):null;
  const aligned=integer(el('alignedSlot').value,1,8);
  el('confirmAlignment').disabled=!setup||aligned===null||(pendingTarget!==null&&aligned%8+1!==pendingTarget)||!el('alignmentConfirm').checked;
  el('saveStepper').disabled=!setup||integer(el('stepsPerSlot').value,1,4096)===null||!['1','-1'].includes(el('stepDirection').value)||!el('stepperConfirm').checked;
  const jog=setup&&state?.canTest&&integer(el('jogSteps').value,1,512)!==null&&el('jogConfirm').checked;
  el('jogBack').disabled=el('jogForward').disabled=!jog;
  el('refillWeek').disabled=!idle||!state?.storageOk||state?.irTestMode||!state?.canRefillWeek||!timesSaved||!el('weekFilledConfirm').checked||!el('weekParkedConfirm').checked;
  el('weekFilledConfirm').disabled=el('weekParkedConfirm').disabled=!idle||Boolean(state?.irTestMode)||!state?.canRefillWeek;
  el('joinWifi').disabled=!setup;
  instructionControls(idle);
  el('scheduleStatus').textContent=!connected?'Connection unavailable. Check the device before making changes.':
    state?.irTestMode?'IR TEST MODE ACTIVE. Daily dispensing is paused. Stop testing and confirm alignment before enabling the schedule.':
    state?.scheduleEnabled?'Daily schedule enabled. Pause before changing settings. '+(state.systemStatus||''):
    !timesValid?'Choose a valid scheduled time and a later overdue time on the same day.':
    !timesSaved?'Times have been edited. Save them before enabling the daily schedule.':
    'Daily schedule paused. '+(state?.systemStatus||'Complete setup before enabling.');
  renderOverview();
}
async function fetchTimed(url,options={}) {
  const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),3500);
  try{return await fetch(url,{...options,signal:controller.signal});}finally{clearTimeout(timer);}
}
async function post(url,fields,target) {
  if(busy||!token||!connected)return;
  busy=true;const usedToken=token;token='';
  if(['/stepper/jog','/schedule/mode','/test/mode'].includes(url))invalidateIr('Waiting for fresh sensor readings after this operation.');
  clearConfirmations();controls();el(target).textContent='Sending…';
  try {
    const response=await fetchTimed(url,{method:'POST',body:new URLSearchParams({...fields,confirm:'1',token:usedToken})});
    el(target).textContent=await response.text();
  } catch {
    el(target).textContent='Response lost. Check the displayed device state and mechanism before retrying.';
  } finally {busy=false;controls();}
}
function list(id,values) {el(id).replaceChildren(...values.map(value=>{const li=document.createElement('li');li.textContent=value;return li;}));}

function instructionTextValid(value) {
  return typeof value==='string'&&value.length<=48&&!/[^\x20-\x21\x23-\x5b\x5d-\x7e]/.test(value);
}
function instructionsAvailable() {
  return Array.isArray(state?.refillInstructions)&&state.refillInstructions.length===7&&state.refillInstructions.every(instructionTextValid);
}
function instructionValues() {return Array.from({length:7},(_,i)=>el('instructionDay'+(i+1)).value);}
function instructionControls(idle) {
  const available=instructionsAvailable();
  const invalid=instructionValues().findIndex(value=>!instructionTextValid(value));
  for(let day=1;day<=7;day++)el('instructionDay'+day).disabled=!available||busy;
  el('saveInstructions').disabled=!idle||!state?.storageOk||!available||invalid!==-1;
  el('instructionsStatus').textContent=!available?'Refill instructions unavailable in this firmware.':
    invalid!==-1?'Day '+(invalid+1)+': use at most 48 printable ASCII characters, excluding double quotes and backslashes.':
    instructionsDirty?'Unsaved changes. Polling will keep your draft until you save.':
    'Instructions are saved on the pillbox. Blank days appear as Not set on the healthcare LCD.';
}
function renderRefillInstructions() {
  if(!instructionsAvailable()||instructionsDirty)return;
  state.refillInstructions.forEach((value,i)=>{el('instructionDay'+(i+1)).value=value;});
}
async function saveRefillInstructions(event) {
  event.preventDefault();
  if(el('saveInstructions').disabled||busy||!token||!connected)return;
  const values=instructionValues();if(!values.every(instructionTextValid))return;
  const usedToken=token;token='';busy=true;clearConfirmations();controls();
  el('instructionsMessage').textContent='Saving instructions...';
  try {
    const fields={confirm:'1',token:usedToken};values.forEach((value,i)=>{fields['day'+(i+1)]=value;});
    const response=await fetchTimed('/refill/instructions',{method:'POST',body:new URLSearchParams(fields)});
    el('instructionsMessage').textContent=await response.text();
    if(response.ok){
      state.refillInstructions=values;instructionsDirty=false;renderRefillInstructions();
    }
  } catch {
    el('instructionsMessage').textContent='Response lost. Your draft is preserved. Reconnect and check the saved instructions before retrying.';
  } finally {busy=false;controls();}
}

function clearIrTimer() {
  if(irDisplayTimer!==null)clearTimeout(irDisplayTimer);
  irDisplayTimer=null;
}
function irInstruction() {
  if(state?.timerTestMode===true) {
    const instructions={
      WAITING:'TIMER TEST: keep your hand clear until the 10-second ready time. An early hand cannot dispense.',
      READY:'TIMER TEST READY: clear the sensor for one second, then hold a fresh hand for 1.5 seconds for one test rotation.',
      OVERDUE:'TEST OVERDUE: a fresh hand can still request this test cycle. Clear for one second, then hold for 1.5 seconds.',
      MOVING:'TIMER TEST: the carousel is moving. Wait for completion.',
      COMPLETED:'TIMER TEST COMPLETED: another hand will not repeat this cycle. After the motor pause, confirm empty/dummy contents and press Restart timer test.'
    };
    return instructions[state.timerTestPhase]||'TIMER TEST: check the current test status before presenting a hand.';
  }
  if(state?.irTestMode)return 'IR TEST MODE: after the three-second motor pause, clear the sensor for one second, then hold a new hand for 1.5 seconds. Repeat only with an empty dispenser or dummy contents.';
  return 'Automatic hand detection: keep the receiving area clear for one second, then hold your hand for 1.5 seconds when today’s dose is ready.';
}
function invalidateIr(message) {
  clearIrTimer();el('irSignal').textContent='Unknown';el('irStatus').textContent=message;
  el('irDiagnostic').textContent='No recent IR reading is available.';controls();
}
function renderIr() {
  clearIrTimer();
  const age=state.irAgeMs,rawValid=state.irRaw===0||state.irRaw===1;
  const directMode=state.irActiveLevel===0&&state.handHoldMs===1500;
  const reported=Number.isFinite(age)&&age>=0&&age<=irDisplayMaxAgeMs&&rawValid&&!state.motionActive&&directMode;
  const steady=reported&&state.irStable===true;
  // This is a last-reported display only; the ESP32 samples and times the hand independently.
  el('irSignal').textContent=steady&&typeof state.irHandDetected==='boolean'?(state.irHandDetected?'Hand detected':'Clear'):reported?'Changing signal':'Unknown';
  el('irStatus').textContent=state.motionActive?'Movement in progress; waiting for new hand-sensor readings.':
    !directMode?'Waiting for the direct IR firmware with fixed 1.5-second hand detection.':irInstruction()+(reported?'':' Waiting for a sensor update.');
  const level=value=>value===0?'LOW (hand)':value===1?'HIGH (clear)':'unavailable';
  el('irDiagnostic').textContent='Last received GPIO14: '+(rawValid?level(state.irRaw):'unavailable')+'. Reported steady interval '+(Number.isFinite(state.irStableMs)?state.irStableMs:0)+' ms. Sample age on receipt '+(Number.isFinite(age)&&age>=0?age+' ms':'unknown');
  if(reported)irDisplayTimer=setTimeout(()=>invalidateIr('No recent sensor update. '+irInstruction()),irDisplayMaxAgeMs+1-age);
}
function timerTestFieldsValid() {
  return typeof state?.timerTestMode==='boolean'&&
    ['OFF','WAITING','READY','OVERDUE','MOVING','COMPLETED'].includes(state?.timerTestPhase)&&
    Number.isInteger(state?.timerTestDueIn)&&state.timerTestDueIn>=0&&
    Number.isInteger(state?.timerTestOverdueIn)&&state.timerTestOverdueIn>=0;
}
function renderTimerTest() {
  const valid=timerTestFieldsValid(),active=valid&&state.timerTestMode===true;
  const phase=valid?state.timerTestPhase:'OFF';
  el('startTimerTest').textContent=active?'Restart timer test':'Start timer test';
  el('timerTestStatus').textContent=!valid?'Timer test unavailable in this firmware.':
    active?(state.irTestStatus||'Timer test active.'):'Ready after 10 seconds; overdue after 30 seconds from Start.';
  el('timerTestReady').textContent=!valid?'Unavailable':!active?'Not started':state.motionActive?'Moving':phase==='WAITING'?state.timerTestDueIn+' s':
    phase==='COMPLETED'?'Test completed':phase==='MOVING'?'Moving':'Ready now';
  el('timerTestDeadline').textContent=!valid?'Unavailable':!active?'Not started':phase==='COMPLETED'?'Test completed':
    phase==='OVERDUE'?'Overdue':state.timerTestOverdueIn+' s';
  el('timerTestAlert').hidden=!active||phase!=='OVERDUE';
  el('timerTestAlert').textContent=active&&phase==='OVERDUE'?(state.motionActive?
    'TEST OVERDUE: the late test rotation is in progress. Wait for completion.':
    'TEST OVERDUE: no test cycle completed within 30 seconds. A fresh hand can still request this test cycle.') :'';
  if(active)el('irTestBanner').textContent=(phase==='OVERDUE'?'TEST OVERDUE: no test cycle completed within 30 seconds. ':'Timer + overdue test active. ')+
    'Daily dispensing is paused. Closing this page does not stop the test. Use Stop test mode to finish.';
}
function renderIrTest() {
  const active=state.irTestMode===true;
  el('irTestBanner').hidden=!active;
  el('irTestBanner').textContent=active?'Test mode active. Daily dispensing is paused. Closing this page does not stop the motor tests.':'';
  el('irTestStatus').textContent=state.irTestStatus||'Waiting for test-mode status.';
  el('irTestGesture').textContent=active?(state.motionActive?state.motionText||'Test movement in progress.':state.handStatus||'Clear the sensor, then present a fresh hand.'):'Test mode is off. Starting it requires an empty dispenser or dummy contents.';
  const cycles=Number.isInteger(state.irTestCycles)&&state.irTestCycles>=0?state.irTestCycles:null;
  el('irTestCount').textContent=cycles===null?'Unavailable':String(cycles);
  renderTimerTest();
}
function renderWeek() {
  const configured=state.weekConfigured===true, due=state.weekRefillDue===true;
  const elapsed=integer(state.weekDay,0,7);
  el('weekProgress').textContent=configured&&elapsed!==null?elapsed+' / 7 scheduled days':'Not started';
  el('weekDates').textContent=configured&&integer(state.weekStartDay,1,1000000)!==null?dayText(state.weekStartDay)+' through '+dayText(state.weekStartDay+6)+', Singapore dates':'Seven medicine compartments; compartment 8 stays empty for parking.';
  el('weekStatus').textContent=state.weekStatus||'Waiting for weekly refill status.';
  el('weekRefillAlert').hidden=!due;
  el('weekRefillAlert').textContent=due?'Weekly refill due. Remove remaining medicine, refill all seven days and park at empty 8. Open Refill to confirm.':'';
  el('weekPanel').classList.toggle('overdue',Boolean(due));
  el('weekNextStart').textContent=Number.isInteger(state.weekNextStartDay)&&state.weekNextStartDay>0?'When confirmed, the next week starts '+dayText(state.weekNextStartDay)+' using the saved daily times. If a daily record already exists today, the next week starts tomorrow.':'Set the clock to show the start date for a new week.';
  el('refillWeek').textContent=configured?'Confirm refill & start next week':'Confirm pack & start week';
  list('weekCompartments',configured&&Number.isInteger(state.weekStartDay)&&state.weekStartDay>0?Array.from({length:7},(_,i)=>'Day '+(i+1)+', '+dayText(state.weekStartDay+i)+', compartment '+(i+1)):['Load Day 1 medicine into compartment 1, through Day 7 into compartment 7. Leave compartment 8 empty.']);
}
function renderToday() {
  const today=state.today||{};
  const descriptions={NOT_DUE:'Waiting for today’s scheduled time.',POSITIONING:'Carousel rotation is in progress.',READY:'Today’s day-specific compartment is armed. Check the status below before presenting a hand.',ATTEMPTED:'Today’s stepper release attempt is recorded. Automatic repeat is blocked.',COMPLETED:'Today’s stepper release command is completed. Another automatic cycle today is blocked.',MISSED:'This scheduled day ended without a completed dose.',FAULT:'An interrupted or failed operation needs inspection. No automatic retry.'};
  el('todayStatus').textContent=state.irTestMode?'Daily dispensing is paused during IR test mode. Saved daily records below are not test results.':!state.clockSynced?'Set the clock to identify today’s dose. Saved records remain below.':descriptions[today.status]||'Waiting for today’s status.';
  el('todaySlot').textContent=Number.isInteger(today.slot)&&today.slot>=1&&today.slot<=8?'Compartment '+today.slot:'Not assigned yet';
  el('todayMedicine').textContent=integer(today.slot,1,7)!==null?'Day '+today.slot+' medicine pack':'No medicine pack due';
  el('todayDetail').textContent=state.irTestMode?'Use the test-mode panel above. Test rotations do not complete or reset a daily dose.':state.motionActive?state.motionText||'Mechanism moving.':state.handStatus||state.systemStatus||'';
  el('lastAction').textContent=today.completedAt?'Completion recorded: '+clockText(today.completedAt):today.preparedAt?'Armed at: '+clockText(today.preparedAt):'';
  el('doseWindow').textContent='Singapore time. Ready at '+timeInput(state.dueMinute)+', overdue alert at '+timeInput(state.alertMinute)+', hand hold '+(state.handHoldMs/1000).toFixed(1)+' seconds';
  const overdue=today.overdue===true&&!today.completedAt;
  el('overdueAlert').hidden=!overdue;
  el('overdueAlert').textContent=overdue?'Dose overdue. Check the dispenser. An eligible late dose remains available until midnight.':'';
  el('todayPanel').className=overdue?'overdue':'';
  const history=Array.isArray(state.history)?[...state.history].sort((a,b)=>b.day-a.day):[];
  list('dailyLog',history.length?history.map(item=>dayText(item.day)+', compartment '+(item.slot||'-')+'. '+(integer(item.slot,1,7)!==null?'Day '+item.slot+' pack':'Saved record')+': '+item.status+(item.overdue?', overdue recorded':'')+(item.completedAt?', completed '+clockText(item.completedAt):'')):['No recorded daily events yet.']);
  const alerts=Array.isArray(state.alerts)?state.alerts:[];
  el('alertSummary').hidden=alerts.length===0;
  el('alertSummary').textContent=alerts.length?alerts.length+' retained overdue record'+(alerts.length===1?'':'s')+' without a completed dose. Review the dated entries below.':'No retained overdue records awaiting completion.';
}
async function refresh() {
  if(!busy) {
    try {
      const started=performance.now();const response=await fetchTimed('/state',{cache:'no-store'});
      if(!response.ok)throw new Error('Status unavailable');
      state=await response.json();token=state.token;connected=true;connectionFailed=false;
      const latency=Math.max(0,Math.ceil(performance.now()-started));
      if(Number.isFinite(state.irAgeMs)&&state.irAgeMs>=0)state.irAgeMs+=latency;
      el('temp').textContent=state.temp===null?'Unavailable':(state.temp/100).toFixed(1)+' °C';
      el('rh').textContent=state.rh===null?'Unavailable':(state.rh/100).toFixed(1)+' %';
      el('environment').textContent=state.environment||'';
      el('tag').textContent=state.tag||'No card scanned';
      el('nfcStatus').textContent=state.nfcStatus||'Waiting for the MSP430 reader.';
      el('storage').textContent=state.storageOk?'':'Settings storage unavailable. Inspect and restart before enabling movement.';
      el('clockStatus').textContent=state.clockSynced?clockText(state.epoch):'Clock not set. Pause the schedule, then sync from this device.';
      const pending=state.today?.status==='READY'&&integer(state.today?.slot,1,8)!==null;
      const preceding=pending?(state.today.slot+6)%8+1:0;
      el('alignmentStatus').textContent=state.positionKnown?'Current aligned/last released compartment: '+state.currentSlot+'. Next physical step reaches compartment '+(state.currentSlot%8+1)+'. Calendar-day eligibility is checked separately; no loaded missed compartment is skipped automatically.':
        pending?'Position unknown. Pause, manually remove any missed medicine, and visually confirm the preceding compartment '+preceding+' to release today’s day-specific compartment '+state.today.slot+'. Do not rotate past a loaded missed compartment.':'Position unknown. With the schedule paused, manually remove missed medicine and verify the current compartment. A new week must start at empty parking compartment 8.';
      el('networkStatus').textContent=state.lanIp?'On your home Wi-Fi: http://pharmavault.local or http://'+state.lanIp+'. On the PharmaVault Wi-Fi: http://pharmavault':'On the PharmaVault Wi-Fi: http://pharmavault (or http://192.168.4.1)';
      el('bridgeStatus').textContent=state.bridgeOnline===true?'Healthcare station connected: pillbox readings/status are sent to the MSP430.':'Healthcare station disconnected. Pillbox sensors and dispensing continue locally.';

      renderIr();renderToday();renderIrTest();renderWeek();renderRefillInstructions();
      if(state.motionActive)clearConfirmations();
      if(firstState) {
        firstState=false;el('scheduleTime').value=timeInput(state.dueMinute);el('deadlineTime').value=timeInput(state.alertMinute);
        el('stepsPerSlot').value=state.stepsPerSlot;el('stepDirection').value=state.stepperDirection;
        el('alignmentDetails').open=!state.positionKnown;

      }
    } catch {
      connected=false;connectionFailed=true;token='';invalidateIr('Dashboard connection lost. Keep hands and moving parts clear.');
      el('bridgeStatus').textContent='Dashboard unreachable. Healthcare-station link status is unknown.';
      el('irTestStatus').textContent='Connection lost. Test mode may still be active on the ESP32; reconnect to stop it or restart the board.';
      if(state?.irTestMode){el('irTestBanner').hidden=false;el('irTestBanner').textContent='LAST RECEIVED: IR TEST MODE ACTIVE. The page cannot confirm its current state. Reconnect to stop it or restart the board.';}
      el('irTestGesture').textContent='No current gesture state is available.';
      el('timerTestStatus').textContent='Connection lost. The timer test may still be active; reconnect to check or stop it.';
      el('timerTestReady').textContent=el('timerTestDeadline').textContent='Unavailable';
      if(!el('timerTestAlert').hidden)el('timerTestAlert').textContent='LAST RECEIVED TEST ALERT: overdue. Reconnect to verify the current test state.';
      el('weekStatus').textContent='Connection lost. Weekly progress and dates are last received; reconnect before refilling or enabling dispensing.';
      if(!el('weekRefillAlert').hidden)el('weekRefillAlert').textContent='LAST RECEIVED ALERT: weekly refill due. Reconnect to verify the current weekly state.';
      el('todayStatus').textContent='Connection lost. Daily operating state is unknown. Check the device.';
      el('clockStatus').textContent='No current clock reading from the ESP32.';
      if(!el('overdueAlert').hidden)el('overdueAlert').textContent='LAST RECEIVED ALERT: dose overdue. Reconnect to check its current status.';
      if(!el('alertSummary').hidden)el('alertSummary').textContent='LAST RECEIVED ALERT: overdue records remain. Reconnect to check their current status.';
    }
  }
  controls();setTimeout(refresh,document.hidden?5000:1000);
}
for(let day=1;day<=7;day++)el('instructionDay'+day).addEventListener('input',()=>{instructionsDirty=true;controls();});
el('refillInstructionForm').addEventListener('submit',saveRefillInstructions);
confirmations.forEach(id=>el(id).addEventListener('change',controls));
['scheduleTime','deadlineTime'].forEach(id=>el(id).addEventListener('input',()=>{el('scheduleConfirm').checked=false;controls();}));
el('alignedSlot').addEventListener('change',()=>{el('alignmentConfirm').checked=false;controls();});
['stepsPerSlot','stepDirection'].forEach(id=>el(id).addEventListener(id==='stepDirection'?'change':'input',()=>{el('stepperConfirm').checked=false;controls();}));
el('jogSteps').addEventListener('input',()=>{el('jogConfirm').checked=false;controls();});
el('startIrTest').addEventListener('click',()=>{
  if(el('startIrTest').disabled)return;
  if(!confirm('Start repeatable IR motor test mode with an empty dispenser or dummy contents? This pauses daily dispensing and cancels any pending daily dose. Completed history stays saved.'))return;
  post('/test/mode',{enabled:'1',unloaded:'1'},'irTestMessage');
});
el('startTimerTest').addEventListener('click',()=>{
  if(el('startTimerTest').disabled)return;
  if(!confirm('Start or restart the 10-second ready / 30-second overdue test with an empty carousel or dummy contents? This pauses daily dispensing and cancels any pending daily dose. The saved daily times and weekly day count are unchanged.'))return;
  post('/test/mode',{enabled:'1',timed:'1',unloaded:'1'},'timerTestMessage');
});
el('stopIrTest').addEventListener('click',()=>{
  if(el('stopIrTest').disabled)return;post('/test/mode',{enabled:'0'},'irTestMessage');
});
el('refillWeek').addEventListener('click',()=>{
  if(el('refillWeek').disabled)return;
  if(!confirm('Start the seven-day cycle on '+dayText(state.weekNextStartDay)+'? Confirm all seven day-specific compartments are refilled, any old medicine is removed, and empty compartment 8 is physically at the dispensing opening. This retains the saved daily times, records parking position 8 and enables the schedule. It does not move the motor.'))return;
  post('/week/refill',{filled:'1',parked:'1'},'weekMessage');
});
el('syncClock').addEventListener('click',()=>{
  if(el('syncClock').disabled)return;const epoch=Math.floor(Date.now()/1000);
  if(!confirm('Set the ESP32 clock to '+clockText(epoch)+'? Check this device’s date and time first.'))return;
  post('/clock',{epoch},'clockMessage');
});
el('saveSchedule').addEventListener('click',()=>{if(!el('saveSchedule').disabled)post('/schedule/save',{dueMinute:minuteValue(el('scheduleTime').value),alertMinute:minuteValue(el('deadlineTime').value)},'scheduleMessage');});
el('enableSchedule').addEventListener('click',()=>{
  if(el('enableSchedule').disabled)return;
  if(!confirm('Enable the daily schedule? Each calendar day keeps its assigned compartment, even after a missed dose. Enabling does not move the motor; a fresh 1.5-second hand presentation requests an eligible release.'))return;
  post('/schedule/mode',{enabled:'1'},'scheduleMessage');
});
el('pauseSchedule').addEventListener('click',()=>{if(!el('pauseSchedule').disabled)post('/schedule/mode',{enabled:'0'},'scheduleMessage');});
el('confirmAlignment').addEventListener('click',()=>{if(!el('confirmAlignment').disabled)post('/stepper/align',{slot:el('alignedSlot').value,verified:'1',handled:'1'},'alignmentMessage');});
el('saveStepper').addEventListener('click',()=>{if(!el('saveStepper').disabled)post('/stepper/save',{stepsPerSlot:el('stepsPerSlot').value,direction:el('stepDirection').value,verified:'1'},'stepperMessage');});
['jogBack','jogForward'].forEach(id=>el(id).addEventListener('click',()=>{
  if(el(id).disabled)return;const steps=Number(el('jogSteps').value)*(id==='jogBack'?-1:1);
  if(!confirm('Jog the carousel '+steps+' half-steps? It moves directly and can release contents. Confirm it is unloaded. This invalidates the recorded alignment.'))return;
  post('/stepper/jog',{steps,clear:'1'},'stepperMessage');
}));
el('wifiForm').addEventListener('submit',event=>{
  event.preventDefault();if(el('joinWifi').disabled)return;post('/wifi',{ssid:el('ssid').value,password:el('wifiPassword').value},'wifiMessage');el('wifiPassword').value='';
});

const viewNames=['Today','Settings'];
const viewAlias={Refill:['Today','weekPanel'],Test:['Settings','irTestPanel']};
let activeView='Today', nextView='Settings', nextSection='';
function showView(name,section='',focusTab=false) {
  if(viewAlias[name]){section=section||viewAlias[name][1];name=viewAlias[name][0];}
  if(!viewNames.includes(name))return;
  activeView=name;
  viewNames.forEach(item=>{
    const selected=item===name;
    el('view'+item).hidden=!selected;
    el('nav'+item).setAttribute('aria-selected',String(selected));
    el('nav'+item).setAttribute('tabindex',selected?'0':'-1');
  });
  if(focusTab)el('nav'+name).focus();
  if(section) {
    if(section==='alignmentSection')el('alignmentDetails').open=true;
    if(section==='motorDetails')el('motorDetails').open=true;
    if(section==='weekPanel')el('refillDetails').open=true;
    el(section).scrollIntoView({block:'start',behavior:'auto'});
  }
}
function renderOverview() {
  const live=connected&&state!==null;
  el('connectionState').textContent=live?'Dispenser connected':connectionFailed?'Connection lost':'Connecting';
  el('connectionState').setAttribute('data-online',String(live));
  el('connectionNotice').hidden=!connectionFailed;
  el('connectionNotice').textContent=connectionFailed?'Connection lost. Controls are unavailable; the dispenser may still be running. Reconnect to check its state.':'';
  el('globalTest').hidden=el('irTestBanner').hidden;
  el('quickPause').disabled=el('pauseSchedule').disabled;
  el('quickStopTest').disabled=el('stopIrTest').disabled;
  el('refillDot').hidden=!state?.weekRefillDue;
  el('testDot').hidden=!state?.irTestMode;
  el('motionNotice').hidden=!live||!state.motionActive;
  el('motionNotice').textContent=live&&state.motionActive?'Carousel moving. Wait for this rotation to finish; another action cannot start yet.':'';
  el('nextAction').disabled=!live;
  if(!state) {
    el('heroTitle').textContent=connectionFailed?'Let’s reconnect.':'Connecting to your dispenser.';
    el('todayDetail').textContent='Join the PharmaVault Wi-Fi and keep the dispenser powered on.';
    return;
  }
  el('summaryDue').textContent=timeInput(state.dueMinute);
  el('summaryAlert').textContent=timeInput(state.alertMinute);
  el('scheduleBadge').textContent=!live?'Last received':state.irTestMode?'Test mode':state.scheduleEnabled?'Enabled':'Paused';
  el('modeBadge').textContent=!live?'Offline':state.motionActive?'Moving':state.irTestMode?'Test mode':state.weekRefillDue?'Refill due':state.scheduleEnabled?'Daily mode':'Paused';
  el('overviewDate').textContent=live&&state.clockSynced?new Intl.DateTimeFormat('en-SG',{timeZone:'Asia/Singapore',weekday:'long',day:'numeric',month:'long'}).format(new Date(state.epoch*1000)):'YOUR DAILY OVERVIEW';
  el('weekShort').textContent=state.weekConfigured?(integer(state.weekDay,0,7)??0)+' / 7 days':'Not started';
  el('weekSummary').textContent=!live?'Last received pack status. Reconnect to verify.':state.weekRefillDue?'All seven scheduled days are finished. Refill the whole pack.':!state.weekConfigured?'Seven medicine compartments. One empty parking position.':dayText(state.weekStartDay)+' to '+dayText(state.weekStartDay+6)+'. Missed days still count';
  const localDay=state.clockSynced?Math.floor((state.epoch+28800)/86400):0;
  const slot=state.weekConfigured&&localDay>=state.weekStartDay&&localDay<state.weekStartDay+7?localDay-state.weekStartDay+1:0;
  el('weekDots').replaceChildren(...Array.from({length:7},(_,i)=>{
    const dot=document.createElement('span'),day=state.weekStartDay+i;
    const record=state.weekConfigured?(state.history||[]).find(r=>r.day===day):null;
    const status=record?.status||'Not completed';
    dot.className='week-dot';dot.textContent=String(i+1);
    dot.setAttribute('data-done',String(status==='COMPLETED'));
    dot.setAttribute('data-missed',String(['MISSED','FAULT','ATTEMPTED'].includes(status)));
    dot.setAttribute('data-current',String(slot===i+1));
    dot.setAttribute('aria-label','Day '+(i+1)+': '+status);
    dot.setAttribute('title','Day '+(i+1)+': '+status);
    return dot;
  }));
  let title='', detail=state.systemStatus||'', action='Manage settings';
  nextView='Settings';nextSection='scheduleSection';
  const record=state.today||{};
  const expected=record.status==='COMPLETED'&&record.slot===slot?slot:slot===1?8:slot?slot-1:8;
  const wrongPosition=state.positionKnown&&state.currentSlot!==expected;
  if(!live) {
    title='Let’s reconnect.';detail='The current operating state is unknown. Reconnect to the dispenser before making changes.';
    action='Settings unavailable';
    el('temp').textContent=el('rh').textContent='-';
    el('environment').textContent='Connection lost. Current readings unavailable.';
  } else if(!state.storageOk) {
    title='The dispenser needs attention.';action='View settings';
  } else if(state.motionActive) {
    title='The carousel is turning.';detail='Wait for the motor command to finish. Keep the mechanism clear.';action='';
  } else if(state.irTestMode) {
    title=state.timerTestMode===true?(state.timerTestPhase==='OVERDUE'?'The timer test is overdue.':state.timerTestPhase==='COMPLETED'?'Timer test complete.':'Timer test is running.'):'Test mode is running.';detail=state.handStatus||state.irTestStatus;action='View test controls';nextView='Test';nextSection=state.timerTestMode===true?'timerTestPanel':'';
  } else if(state.weekRefillDue) {
    title='Time for a fresh week.';detail='Clear remaining medicine, refill all seven days and park at empty compartment 8.';action='Prepare weekly refill';nextView='Refill';nextSection='';
  } else if(!state.clockSynced) {
    title='Sync the clock to begin.';detail='Your saved timer is still here. Restore Singapore time after power-up.';action='Set up clock';
  } else if((state.systemStatus||'').includes('Clock is earlier')) {
    title='Check the date and time.';action='Correct clock';
  } else if(!state.stepperVerified) {
    title='Set up the carousel.';detail='Verify one-compartment travel and direction on the unloaded mechanism.';action='Open motor setup';nextSection='motorDetails';
  } else if(!state.weekConfigured) {
    title='Prepare your seven-day pack.';detail='Fill days 1 to 7, leave position 8 empty, then confirm once for the whole week.';action='Set up weekly pack';nextView='Refill';nextSection='';
  } else if(['FAULT','ATTEMPTED','MISSED'].includes(record.status)) {
    title='Today’s dose needs checking.';detail='An unsuccessful or uncertain attempt cannot automatically retry. Inspect the medicine and mechanism.';action='Review alignment';nextSection='alignmentSection';
  } else if(!state.positionKnown||wrongPosition) {
    title='Check carousel alignment.';detail='Pause and remove any missed medicine before manually aligning the correct compartment. Do not rotate past loaded missed doses.';action='Review alignment';nextSection='alignmentSection';
  } else if(!state.scheduleEnabled) {
    title='Your daily schedule is paused.';detail='Check the clock and alignment, then enable the saved timer. Enabling does not move the motor.';action='Enable in settings';
  } else if(record.status==='COMPLETED') {
    title='All set for today.';detail='Today’s motor cycle is recorded. Another daily cycle is blocked until the next scheduled day.';action='';
  } else if(record.status==='READY') {
    title=el('irSignal').textContent==='Unknown'?'Waiting for the hand sensor.':!state.handArmed?'Clear the sensor to get ready.':'Ready for your hand.';
    detail=state.handStatus||'Clear the area for one second, then hold your hand near the sensor for 1.5 seconds.';action='';
  } else {
    title='Your timer is set.';detail='Today’s assigned dose becomes available at '+timeInput(state.dueMinute)+'. Then hold a fresh hand for 1.5 seconds to dispense.';action='';
    if(localDay<state.weekStartDay)detail='Your seven-day pack starts '+dayText(state.weekStartDay)+' at '+timeInput(state.dueMinute)+'. No action needed until then.';
  }
  el('heroTitle').textContent=title;el('todayDetail').textContent=detail;
  el('nextAction').hidden=!action;el('nextAction').textContent=action;
}
viewNames.forEach((name,index)=>{
  el('nav'+name).addEventListener('click',()=>showView(name));
  el('nav'+name).addEventListener('keydown',event=>{
    const offsets={ArrowRight:1,ArrowLeft:-1};
    if(event.key in offsets){event.preventDefault();showView(viewNames[(index+offsets[event.key]+viewNames.length)%viewNames.length],'',true);}
    else if(event.key==='Home'||event.key==='End'){event.preventDefault();showView(viewNames[event.key==='Home'?0:viewNames.length-1],'',true);}
  });
});
el('manageSchedule').addEventListener('click',()=>showView('Settings','scheduleSection',true));
el('openRefill').addEventListener('click',()=>showView('Refill'));
document.querySelectorAll('.jump a').forEach(a=>a.addEventListener('click',event=>{event.preventDefault();showView('Settings',a.getAttribute('href').slice(1));}));
el('nextAction').addEventListener('click',()=>{if(!el('nextAction').disabled)showView(nextView,nextSection,true);});
el('quickPause').addEventListener('click',()=>{
  if(!el('quickPause').disabled)post('/schedule/mode',{enabled:'0'},'globalActionMessage');
});
el('quickStopTest').addEventListener('click',()=>{
  if(!el('quickStopTest').disabled)post('/test/mode',{enabled:'0'},'globalActionMessage');
});
showView('Today');


refresh();

</script></body></html>

)PVHTML";
static_assert(sizeof(DashboardHtml)-1==PV_DASHBOARD_SRC_LEN,"Dashboard HTML changed: run python web/build_web.py");

// Methods stay inside the class so Arduino does not generate invalid forward declarations.
class PharmaVault {
 private:
  static constexpr uint8_t RetiredServo=3,IrPin=14,RetiredTrigger=13,UartRx=4,UartTx=17,SlotCount=8;
  static constexpr uint8_t Step1=5,Step2=6,Step3=7,Step4=8;
  static constexpr uint32_t NetworkMagic=0x50564E31,WeekMagic=0x50565731;
  static constexpr uint32_t StepperMagic=0x50565331;
  static constexpr uint32_t ScheduleMagic=0x50564431,JournalMagic=0x50564A33,PreviousJournalMagic=0x50564A32,LegacyJournalMagic=0x50564A31;
  static constexpr uint32_t TicketLifetimeMs=60000,CooldownMs=3000,IrPollMs=20,IrStableForMs=500;
  static constexpr uint32_t IrFreshMs=500,IrMaxGapMs=250,HandClearMs=1000,ReaderFreshMs=16000;
  // Direct digital IR module: LOW means detected. No website reference capture is used.
  static constexpr uint8_t IrHandLevel=LOW,IrClearLevel=HIGH;
  static constexpr uint16_t StepIntervalMs=3,HandHoldMs=1500;
  enum class Phase:uint8_t {Idle,Queued,Stepping};
  enum class Motion:uint8_t {Jog,Dose,IrTest};
  // Optional countdown flavor of the existing IR test; never changes the real clock.
  enum class TimerTestStage:uint8_t {Waiting,Ready,Overdue,Completed};
  static constexpr uint32_t TimerTestDueMs=10000,TimerTestOverdueMs=30000;
  enum class DayState:uint8_t {None,Preparing,Ready,Attempted,Completed,Missed,Fault};
  struct NetworkSettings {uint32_t magic;char ssid[33],password[65];};
  // flags bit 0 latches the weekly refill reminder, even after a clock change.
  struct WeekSettings {uint32_t magic,firstDay,confirmedAt;uint8_t flags,reserved[3];};
  struct StepperSettings {uint32_t magic;uint16_t stepsPerSlot;int8_t direction;uint8_t verified;};
  struct ScheduleSettings {uint32_t magic;uint16_t dueMinute,alertMinute;};
  // Day labels are historical snapshots; old firmware history remains readable.
  struct DailyRecord {uint32_t day,preparedAt,completedAt;uint8_t slot,state,overdue,reserved;char label[17];uint8_t padding[3];};
  struct DailyJournal {uint32_t magic,maxDay;DailyRecord records[SlotCount];};
  WebServer web{80};DNSServer dns;PvWirelessSerial msp;Preferences prefs;
  NetworkSettings network{};WeekSettings week{};
  char statusChunk[2200]{}; // Reused on the main loop; keep large status buffers off its stack.
  static constexpr uint32_t RefillTextMagic=0x50564D31;
  struct RefillTextSettings {uint32_t magic;char text[7][49];};
  RefillTextSettings refillText{};
  PvPillboxSensors sensors;
  uint32_t providerReportAt=0;uint8_t providerReportPart=0;
  StepperSettings stepper{StepperMagic,512,1,0};ScheduleSettings schedule{ScheduleMagic,780,840};
  DailyJournal journal{};
  bool storageOk=false,hadMotion=false,apStarted=false,wasConnected=false,networkQueued=false;
  bool irTestMode=false;uint32_t irTestCycles=0;
  bool timerTestMode=false;uint32_t timerTestStartedAt=0;
  TimerTestStage timerTestStage=TimerTestStage::Waiting;
  bool scheduleEnabled=false,positionKnown=false,clockSynced=false,handArmed=false,handClearSeen=false,readerSeen=false;
  uint8_t currentSlot=0,targetSlot=0,coilIndex=0;
  uint32_t finishedAt=0,stepAt=0,handClearAt=0,motionDay=0,readerAt=0;
  uint16_t stepsRemaining=0;
  int8_t moveDirection=1;
  Phase phase=Phase::Idle;Motion motion=Motion::Jog;
  const char *lastAction="Scheduler paused. Complete setup before enabling.",*nfcLastAction="No weekly-status card scanned yet.";
  char accessPassword[33]{},apSsid[32]{},token[33]{},lastUid[21]{};
  uint32_t tokenAt=0;
  int32_t temperature=-32768,humidity=65535;uint16_t environmentFlags=4;uint32_t environmentAt=0;bool environmentSeen=false;
  int8_t irRaw=-1;bool irHasSample=false;uint32_t irAt=0,irCandidateAt=0,lastIrPoll=0;
  uint64_t clockEpoch=0;uint32_t clockTick=0,clockRemainder=0;

  static bool elapsed(uint32_t now,uint32_t then,uint32_t duration){return now-then>=duration;}
  static bool validLabel(const char *name){
    const size_t n=strlen(name);if(!n||n>16)return false;bool nonspace=false;
    for(size_t i=0;i<n;++i){const unsigned char c=name[i];if(c<32||c>126||c=='"'||c=='\\')return false;if(c!=' ')nonspace=true;}
    return nonspace;
  }
  static bool validRefillText(const char *s){
    if(strlen(s)>48)return false;
    for(size_t i=0;s[i];++i){const unsigned char c=s[i];if(c<32||c>126||c=='"'||c=='\\')return false;}
    return true;
  }
  static void randomHex(char *out){for(size_t i=0;i<4;++i)snprintf(out+8*i,9,"%08lX",static_cast<unsigned long>(esp_random()));}
  bool canTest(uint32_t now)const{return storageOk&&phase==Phase::Idle&&(!hadMotion||elapsed(now,finishedAt,CooldownMs));}
  bool irFresh(uint32_t now)const{return irHasSample&&now-irAt<=IrFreshMs;}
  bool irStable(uint32_t now)const{return phase==Phase::Idle&&irFresh(now)&&now-irAt<=IrMaxGapMs&&now-irCandidateAt>=IrStableForMs;}
  void resetHandGesture(){handArmed=false;handClearSeen=false;handClearAt=0;}
  void invalidateIr(){irHasSample=false;irRaw=-1;irCandidateAt=0;resetHandGesture();}
  void pauseSchedule(const char *reason){
    if(irTestMode&&irTestCycles)positionKnown=false;
    irTestMode=false;timerTestMode=false;scheduleEnabled=false;resetHandGesture();lastAction=reason;
  }
  bool canEnableIrTest(uint32_t now)const{return !irTestMode&&stepper.verified&&canTest(now);}
  const char *timerTestPhase()const{
    if(!timerTestMode)return "OFF";
    if(timerTestStage==TimerTestStage::Overdue)return "OVERDUE";
    if(phase!=Phase::Idle)return "MOVING";
    switch(timerTestStage){
      case TimerTestStage::Waiting:return "WAITING";
      case TimerTestStage::Ready:return "READY";
      case TimerTestStage::Overdue:return "OVERDUE";
      default:return "COMPLETED";
    }
  }
  uint32_t timerTestSecondsUntil(uint32_t now,uint32_t duration)const{
    if(!timerTestMode||timerTestStage==TimerTestStage::Completed)return 0;
    const uint32_t passed=now-timerTestStartedAt;
    return passed>=duration?0:(duration-passed+999)/1000;
  }
  void serviceTimerTest(uint32_t now){
    if(!timerTestMode||timerTestStage==TimerTestStage::Completed)return;
    if(timerTestStage==TimerTestStage::Waiting&&elapsed(now,timerTestStartedAt,TimerTestDueMs)){
      timerTestStage=TimerTestStage::Ready;invalidateIr();
      lastAction="TIMER TEST READY: clear the sensor for one second, then hold a hand for 1.5 seconds.";
    }
    if(timerTestStage==TimerTestStage::Ready&&elapsed(now,timerTestStartedAt,TimerTestOverdueMs)){
      timerTestStage=TimerTestStage::Overdue;
      lastAction="TEST OVERDUE: no test rotation completed. One late hand-triggered test is still allowed.";
    }
  }
  const char *irTestStatus(uint32_t now)const{
    if(!irTestMode)return "IR test mode is off. Daily records are unchanged by test cycles.";
    if(timerTestMode){
      if(phase!=Phase::Idle)return "TIMER TEST: carousel rotating once. This is a test command, not a daily dose.";
      if(timerTestStage==TimerTestStage::Completed)return "TIMER TEST COMPLETE: no more movement until Start / restart timer test is pressed.";
      if(timerTestStage==TimerTestStage::Waiting)return "TIMER TEST: waiting for the 10-second availability time. Hand detection cannot move the motor yet.";
      if(timerTestStage==TimerTestStage::Overdue)return "TEST OVERDUE: remove your hand for one second, then hold for 1.5 seconds for one late test cycle.";
      if(!handArmed)return "TIMER TEST READY: remove your hand and keep the sensor clear for one second.";
      return "TIMER TEST READY: hold a fresh hand for 1.5 seconds. The test becomes overdue at 30 seconds.";
    }
    if(phase!=Phase::Idle)return "TEST MODE: carousel rotating. Remove your hand before the next test.";
    if(!canTest(now))return "TEST MODE: wait for the three-second motor pause, then keep the sensor clear.";
    if(!handArmed)return "TEST MODE: remove your hand and keep the sensor clear for one second.";
    return "TEST MODE READY: hold your hand for 1.5 seconds to advance one compartment.";
  }
  // Advance wall time from monotonic millis, including its rollover. No time survives loss of power.
  void serviceClock(uint32_t now){
    if(!clockSynced)return;
    const uint64_t delta=uint64_t(now-clockTick)+clockRemainder;clockTick=now;
    clockEpoch+=delta/1000;clockRemainder=uint32_t(delta%1000);
  }
  uint32_t localDay()const{return clockSynced?uint32_t((clockEpoch+28800ULL)/86400ULL):0;}
  uint16_t localMinute()const{return uint16_t(((clockEpoch+28800ULL)%86400ULL)/60ULL);}
  const DailyRecord *recordFor(uint32_t day)const{
    if(!day)return nullptr;const DailyRecord &r=journal.records[day%SlotCount];return r.day==day?&r:nullptr;
  }
  static DayState stateOf(const DailyRecord *r){return r?static_cast<DayState>(r->state):DayState::None;}
  static const char *dayStateName(DayState s){
    switch(s){case DayState::Preparing:return "POSITIONING";case DayState::Ready:return "READY";case DayState::Attempted:return "ATTEMPTED";
      case DayState::Completed:return "COMPLETED";case DayState::Missed:return "MISSED";case DayState::Fault:return "FAULT";default:return "NOT_DUE";}
  }
  bool saveBytes(const char *key,const void *data,size_t size){
    if(!storageOk)return false;if(prefs.putBytes(key,data,size)==size)return true;
    storageOk=false;pauseSchedule("Settings write failed. Automatic operation stopped; inspect and restart.");return false;
  }
  // Journal is committed BEFORE any scheduled movement or dispense attempt. Never retry uncertain output.
  bool saveRecord(const DailyRecord &record){
    DailyJournal updated=journal;updated.magic=JournalMagic;updated.records[record.day%SlotCount]=record;
    if(record.day>updated.maxDay)updated.maxDay=record.day;
    if(!saveBytes("dailyLog1",&updated,sizeof(updated)))return false;journal=updated;return true;
  }
  bool changeRecord(uint32_t day,DayState next){
    const DailyRecord *previous=recordFor(day);if(!previous)return false;
    DailyRecord r=*previous;r.state=static_cast<uint8_t>(next);
    if(next==DayState::Completed)r.completedAt=uint32_t(clockEpoch);
    return saveRecord(r);
  }
  // A pending day is invalidated by physical maintenance, rather than moved to an unverified compartment.
  bool invalidatePending(){
    // Maintenance can release pills even before the clock has been restored after reboot.
    // Invalidate all retained prepared days BEFORE allowing physical movement or refill edits.
    for(uint8_t i=0;i<SlotCount;++i){const DailyRecord r=journal.records[i];
      if(r.day&&r.state==uint8_t(DayState::Ready)&&!changeRecord(r.day,DayState::Fault))return false;
    }
    return true;
  }
  bool weekConfigured()const{return week.magic==WeekMagic&&week.firstDay!=0;}
  bool weekRefillDue()const{return weekConfigured()&&(week.flags&1);}
  uint8_t calendarSlot()const{
    if(!clockSynced||!weekConfigured()||localDay()<week.firstDay||localDay()-week.firstDay>=7)return 0;
    return uint8_t(localDay()-week.firstDay+1);
  }
  uint8_t weekDay()const{
    if(!weekConfigured()||!clockSynced||localDay()<week.firstDay)return 0;
    const uint32_t days=localDay()-week.firstDay+(localMinute()>=schedule.dueMinute?1:0);
    return uint8_t(days>7?7:days);
  }
  uint32_t weekNextStartDay()const{return clockSynced?localDay()+(recordFor(localDay())?1:0):0;}
  bool canRefillWeek(uint32_t now)const{
    return !irTestMode&&clockSynced&&localDay()>=journal.maxDay&&stepper.verified&&canTest(now)&&(!weekConfigured()||weekRefillDue());
  }
  uint8_t expectedPosition()const{
    const uint8_t slot=calendarSlot();
    if(!slot)return 8;
    const DailyRecord *r=recordFor(localDay());
    if(stateOf(r)==DayState::Completed&&r->slot==slot)return slot;
    return slot==1?8:uint8_t(slot-1);
  }
  const char *weekStatus()const{
    if(!weekConfigured())return "Load all seven day-specific compartments; leave compartment 8 empty and parked. Confirm the whole week once below.";
    if(weekRefillDue())return "REFILL ALL 7 DAYS: the seven scheduled days are finished. Remove remaining doses, refill days 1-7 and park at empty compartment 8.";
    if(!clockSynced)return "Weekly pack and timer saved. Sync the clock to restore the correct day.";
    if(localDay()<week.firstDay)return "Seven-day pack ready. The first scheduled day starts on the displayed start date.";
    if(localDay()>week.firstDay+6)return "Week finished. Waiting to save the refill reminder.";
    return "Day-specific pack active. No daily refill, medicine-name entry or NFC tap is required. The saved timer repeats each day.";
  }
  void updateWeek(){
    if(!storageOk||!weekConfigured()||weekRefillDue()||!clockSynced)return;
    const DailyRecord *last=recordFor(week.firstDay+6);
    const bool complete=last&&last->slot==7&&stateOf(last)==DayState::Completed;
    if(localDay()>week.firstDay+6||complete){
      WeekSettings updated=week;updated.flags|=1;
      if(!saveBytes("week1",&updated,sizeof(updated)))return;
      week=updated;scheduleEnabled=false;resetHandGesture();
      lastAction="Weekly refill due. Refill all seven day-specific compartments and confirm the empty parking position.";
    }
  }
  bool readyToEnable(uint32_t now)const{
    if(irTestMode||!clockSynced||!weekConfigured()||weekRefillDue()||localDay()<journal.maxDay||localDay()>week.firstDay+6||!positionKnown||!stepper.verified||!canTest(now))return false;
    const DailyRecord *r=recordFor(localDay());
    if(stateOf(r)==DayState::Ready&&r->slot!=calendarSlot())return false;
    return currentSlot==expectedPosition();
  }
  const char *systemStatus(uint32_t now)const{
    if(!storageOk)return "Fault: automatic operation stopped. Inspect power, mechanism and saved settings.";
    if(irTestMode)return irTestStatus(now);
    if(weekRefillDue())return weekStatus();
    if(!clockSynced)return "Set the clock from your laptop after power-up. Time zone is Singapore (UTC+8).";
    if(localDay()<journal.maxDay)return "Clock is earlier than an already recorded day. Correct the date; history cannot be replayed.";
    if(phase!=Phase::Idle)return "Motor command in progress. Position and pill delivery are not physically sensed.";
    if(!weekConfigured())return weekStatus();
    if(!stepper.verified)return "Test and save the step count and direction for one compartment.";
    if(!positionKnown)return "Confirm the actual carousel compartment after power-up or manual movement. Remove missed doses before realigning.";
    if(currentSlot!=expectedPosition())return "Rotation blocked: today's compartment is not next. Pause, remove any missed doses, manually align the predecessor compartment and confirm alignment.";
    if(!scheduleEnabled)return "Daily schedule paused. Enable once after setup; the saved timer repeats throughout this week.";
    if(localDay()<week.firstDay)return "Weekly pack ready. Waiting for its first scheduled day; timer unchanged.";
    const DailyRecord *r=recordFor(localDay());
    if(stateOf(r)==DayState::Ready)return r->overdue?"Overdue: no completed dispense command by the alert time. One late hand-triggered cycle remains available today.":"Today's assigned compartment is ready. Hold your hand for 1.5 seconds to dispense.";
    if(stateOf(r)==DayState::Completed)return "Today's dispense command completed. No further automatic cycle is allowed today.";
    if(stateOf(r)==DayState::Fault||stateOf(r)==DayState::Attempted)return "Today's action is uncertain or failed. Inspect manually; automatic retry is blocked.";
    return "Waiting for the saved daily time. The carousel moves only after hand detection.";
  }
  const char *handStatus(uint32_t now)const{
    if(irTestMode)return irTestStatus(now);
    if(!scheduleEnabled)return "Hand dispensing paused with the scheduler.";
    if(phase!=Phase::Idle)return "A motor cycle is running; hand presentations are not queued.";
    const DailyRecord *r=recordFor(localDay());
    if(stateOf(r)!=DayState::Ready)return stateOf(r)==DayState::Completed?"Already dispensed by command today. Wait for the next daily compartment.":"No daily dose is available for a hand-triggered rotation.";
    if(!positionKnown||recordFor(localDay())->slot!=calendarSlot()||currentSlot!=expectedPosition())return "Blocked: pause, remove missed doses and confirm the position immediately before today's compartment.";
    if(localMinute()<schedule.dueMinute)return "Wait for the configured daily availability time.";
    if(!canTest(now))return "Wait for the three-second motor pause, then clear the sensor for one second.";
    if(!handArmed)return "Remove your hand and keep the sensor clear for one second.";
    return "Ready: hold your hand under the outlet continuously for 1.5 seconds.";
  }
  const char *irStatus(uint32_t now)const{
    if(phase!=Phase::Idle)return "IR gesture suspended during motor movement.";
    if(!irFresh(now))return "Waiting for a fresh IR reading.";
    if(!irStable(now))return "Waiting for a steady IR signal.";
    return irRaw==IrHandLevel?"Hand/object detected at the outlet.":"Receiving area clear.";
  }
  bool readerFresh(uint32_t now)const{return msp.online()&&readerSeen&&now-readerAt<=ReaderFreshMs;}
  const char *nfcStatus(uint32_t now)const{if(!msp.online())return "Wireless FireBeetle bridge offline. Check its USB power and saved pairing; the MSP430 remains locally connected to the bridge.";return readerFresh(now)?"Tap the tag attached to this paired pillbox. The healthcare LCD shows status and all seven refill instructions; a tap never dispenses or resets the week.":"Waiting for the optional MSP430 weekly-status card reader.";}

  void loadSettings(){
    storageOk=prefs.begin("pharmavault",false);strcpy(accessPassword,"PharmaVault67");  // fixed AP and dashboard password
    if(!storageOk)return;
    if(prefs.getBytesLength("network")==sizeof(network))prefs.getBytes("network",&network,sizeof(network));
    if(network.magic!=NetworkMagic||!memchr(network.ssid,0,sizeof(network.ssid))||!memchr(network.password,0,sizeof(network.password))||!network.ssid[0]||strlen(network.password)>63||(network.password[0]&&strlen(network.password)<8))network=NetworkSettings{};
    // Ignore former hand1/hold1 preferences: direct LOW detection and 1500ms are fixed.
    if(prefs.getBytesLength("stepper1")==sizeof(stepper))prefs.getBytes("stepper1",&stepper,sizeof(stepper));
    if(stepper.magic!=StepperMagic||!stepper.stepsPerSlot||stepper.stepsPerSlot>4096||(stepper.direction!=1&&stepper.direction!=-1)||stepper.verified>1)stepper=StepperSettings{StepperMagic,512,1,0};
    if(prefs.getBytesLength("schedule1")==sizeof(schedule))prefs.getBytes("schedule1",&schedule,sizeof(schedule));
    if(schedule.magic!=ScheduleMagic||schedule.dueMinute>=schedule.alertMinute||schedule.alertMinute>1439)schedule=ScheduleSettings{ScheduleMagic,780,840};
    const size_t journalSize=prefs.getBytesLength("dailyLog1");
    if(journalSize){
      // Refuse corrupt existing history instead of silently allowing another dispensing attempt.
      if(journalSize!=sizeof(journal)){storageOk=false;return;}
      prefs.getBytes("dailyLog1",&journal,sizeof(journal));
      if(journal.magic!=JournalMagic&&journal.magic!=PreviousJournalMagic&&journal.magic!=LegacyJournalMagic){storageOk=false;return;}
      uint32_t maximumStoredDay=0;const DailyRecord blank{};
      for(uint8_t i=0;i<SlotCount;++i){const DailyRecord &r=journal.records[i];
        if(!r.day){if(memcmp(&r,&blank,sizeof(r))){storageOk=false;return;}continue;}
        if(r.day%SlotCount!=i||r.day>journal.maxDay||r.slot<1||r.slot>8||r.state<1||r.state>6||r.overdue>1||r.reserved||r.padding[0]||r.padding[1]||r.padding[2]||!memchr(r.label,0,17)||!validLabel(r.label)||
          (r.state==uint8_t(DayState::Completed))!=(r.completedAt!=0)){storageOk=false;return;}
        if(r.day>maximumStoredDay)maximumStoredDay=r.day;
      }
      if(maximumStoredDay!=journal.maxDay||(journal.maxDay&&!recordFor(journal.maxDay))){storageOk=false;return;}
    }
    // The journal version records the motion semantics. Older firmware rejects the new magic.
    // Convert ALL legacy Ready records and uncertain attempts in ONE write; never expose
    // an intermediate new-version journal containing an already-positioned legacy Ready.
    const bool legacy=journalSize&&journal.magic!=JournalMagic;
    DailyJournal updated=journal;updated.magic=JournalMagic;
    bool changed=!journalSize||legacy;
    for(uint8_t i=0;i<SlotCount;++i){DailyRecord &r=updated.records[i];
      if(r.day&&(r.state==uint8_t(DayState::Preparing)||r.state==uint8_t(DayState::Attempted)||(legacy&&r.state==uint8_t(DayState::Ready)))){
        r.state=uint8_t(DayState::Fault);changed=true;
      }
    }
    if(changed&&!saveBytes("dailyLog1",&updated,sizeof(updated)))return;
    journal=updated;
    const size_t weekSize=prefs.getBytesLength("week1");
    if(weekSize){
      if(weekSize!=sizeof(week)||prefs.getBytes("week1",&week,sizeof(week))!=sizeof(week)){storageOk=false;return;}
      // Epoch-day bounds agree with the accepted 2020-2099 clock range.
      const uint32_t confirmedDay=uint32_t((uint64_t(week.confirmedAt)+28800ULL)/86400ULL);
      if(week.magic!=WeekMagic||week.firstDay<18262||week.firstDay>47483||week.confirmedAt<1577836800UL||week.confirmedAt>=4102444800UL||
        (week.firstDay!=confirmedDay&&week.firstDay!=confirmedDay+1)||(week.flags&~1)||week.reserved[0]||week.reserved[1]||week.reserved[2]){storageOk=false;return;}
    }
    // Refill instructions are informational and do not alter the daily journal.
    RefillTextSettings loaded{};
    bool labelsOk=prefs.getBytesLength("refillText1")==sizeof(loaded)&&
      prefs.getBytes("refillText1",&loaded,sizeof(loaded))==sizeof(loaded)&&loaded.magic==RefillTextMagic;
    for(uint8_t i=0;i<7&&labelsOk;++i)labelsOk=memchr(loaded.text[i],0,49)&&validRefillText(loaded.text[i]);
    if(labelsOk)refillText=loaded;
    // Old servo/contents settings are ignored. Physical position is unknown at boot.
  }
  void writeCoils(uint8_t mask){digitalWrite(Step1,mask&1?HIGH:LOW);digitalWrite(Step2,mask&2?HIGH:LOW);digitalWrite(Step3,mask&4?HIGH:LOW);digitalWrite(Step4,mask&8?HIGH:LOW);}
  bool uartSend(const char *body){char frame[PV_FRAME_SIZE];const size_t n=pv_encode(body,frame,sizeof(frame));return n&&msp.availableForWrite()>=int(n)&&msp.write(reinterpret_cast<const uint8_t*>(frame),n)==n;}
  void sendLcd(const char *code,int slot){char body[64];snprintf(body,sizeof(body),"PV1,0,UI,%s,%d",code,slot);uartSend(body);}
  void finishMotion(){
    writeCoils(0);phase=Phase::Idle;finishedAt=millis();hadMotion=true;invalidateIr();
    if(motion==Motion::Dose){
      currentSlot=targetSlot;positionKnown=true;
      if(!changeRecord(motionDay,DayState::Completed)){positionKnown=false;sendLcd("MOTOR_FAULT",-1);return;}
      lastAction="Today's stepper command completed. Check the actual pill release; no further automatic rotation today.";
      sendLcd("DOSE_STEPS_DONE",targetSlot-1);updateWeek();
    }else if(motion==Motion::IrTest){
      if(targetSlot)currentSlot=targetSlot;
      positionKnown=irTestMode&&targetSlot!=0;
      if(irTestCycles<UINT32_MAX)++irTestCycles;
      lastAction=irTestMode?"IR test rotation completed. No daily dose was recorded. Remove your hand, wait, then test again.":"IR test rotation completed; test mode is off. Reconfirm alignment before daily use.";
      if(timerTestMode){
        timerTestStage=TimerTestStage::Completed;
        lastAction="Timer test rotation completed. Press Start / restart timer test to repeat; daily dose count was not increased.";
      }
      sendLcd("TEST_COMPLETE",targetSlot?targetSlot-1:-1);
    }else{positionKnown=false;lastAction="Stepper jog finished. Visually confirm and record the current compartment.";}
  }
  // Nonblocking half-steps. A late loop never bursts missed steps to catch up.
  void runMotion(){
    invalidateIr();const uint32_t now=millis();
    switch(phase){
      case Phase::Queued:
        phase=Phase::Stepping;stepAt=now;
        if(motion==Motion::Dose)sendLcd("DOSE_MOVING",targetSlot-1);
        else if(motion==Motion::IrTest)sendLcd("TEST_ROTATING",targetSlot?targetSlot-1:-1);
        break;
      case Phase::Stepping:
        if(elapsed(now,stepAt,StepIntervalMs)){
          static const uint8_t masks[8]={1,3,2,6,4,12,8,9};stepAt=now;
          // Hold the final coil phase for a full interval before switching it off.
          if(!stepsRemaining){finishMotion();break;}
          coilIndex=uint8_t((int(coilIndex)+int(moveDirection)+8)%8);writeCoils(masks[coilIndex]);
          --stepsRemaining;
        }break;
      case Phase::Idle:break;
    }
  }
  void pollIr(uint32_t now){
    if(phase!=Phase::Idle){invalidateIr();return;}if(!elapsed(now,lastIrPoll,IrPollMs))return;lastIrPoll=now;
    const int8_t raw=digitalRead(IrPin)==LOW?0:1;
    if(!irHasSample||now-irAt>IrMaxGapMs){resetHandGesture();irCandidateAt=now;}else if(raw!=irRaw)irCandidateAt=now;
    irRaw=raw;irAt=now;irHasSample=true;
  }
  void updateDeadlines(){
    if(!clockSynced||!storageOk)return;const uint32_t day=localDay();
    // Dates still pass while paused or powered off. Record absent past days as missed,
    // never issue catch-up movements or imply pill delivery during that absence.
    if(weekConfigured()&&day>=journal.maxDay){
      for(uint8_t i=0;i<7;++i){const uint32_t missedDay=week.firstDay+i;
        if(missedDay>=day||recordFor(missedDay))continue;
        DailyRecord missed{};missed.day=missedDay;missed.slot=i+1;
        missed.state=uint8_t(DayState::Missed);missed.overdue=1;
        snprintf(missed.label,sizeof(missed.label),"Day %u",unsigned(i+1));
        if(!saveRecord(missed))return;
      }
    }
    for(uint8_t i=0;i<SlotCount;++i){DailyRecord r=journal.records[i];if(!r.day||r.day>day||r.state==uint8_t(DayState::Completed))continue;
      bool changed=false;
      if(r.day<day&&r.state==uint8_t(DayState::Ready)){r.state=uint8_t(DayState::Missed);changed=true;}
      if(!r.overdue&&(r.day<day||localMinute()>=schedule.alertMinute)){r.overdue=1;changed=true;}
      if(changed&&!saveRecord(r))return;
    }
  }
  void serviceDaily(uint32_t){
    updateWeek();
    // Pausing prevents hand/motor actions, not the date's availability/overdue record.
    if(irTestMode||!clockSynced||!storageOk||!weekConfigured()||weekRefillDue()||phase!=Phase::Idle)return;
    const uint32_t day=localDay();if(day<journal.maxDay){pauseSchedule("Clock moved before an already recorded day. Correct it; no automatic replay.");return;}
    const uint8_t next=calendarSlot();
    if(!next||recordFor(day)||localMinute()<schedule.dueMinute)return;
    DailyRecord r{};r.day=day;r.slot=next;r.state=uint8_t(DayState::Ready);r.preparedAt=uint32_t(clockEpoch);
    r.overdue=localMinute()>=schedule.alertMinute?1:0;snprintf(r.label,sizeof(r.label),"Day %u",unsigned(next));
    // Availability belongs to this DATE even if physical alignment blocks movement.
    // A missed prior day never substitutes its pills for today's assigned compartment.
    if(!saveRecord(r))return;
    token[0]=0;invalidateIr();
    lastAction=currentSlot==expectedPosition()&&positionKnown?"Today's assigned dose is available. Hold a fresh hand for 1.5 seconds to dispense.":"Today's dose is available but alignment blocks rotation. Pause, remove missed doses and confirm the predecessor position.";
    sendLcd("DOSE_READY",next-1);
  }
  bool handGestureReady(uint32_t now){
    if(!canTest(now)||!irFresh(now)||now-irAt>IrMaxGapMs){resetHandGesture();return false;}
    if(irRaw==IrClearLevel){
      if(!handClearSeen){handClearSeen=true;handClearAt=now;}
      if(irStable(now)&&elapsed(now,handClearAt,HandClearMs))handArmed=true;
      return false;
    }
    handClearSeen=false;
    return irRaw==IrHandLevel&&handArmed&&irStable(now)&&elapsed(now,irCandidateAt,HandHoldMs);
  }
  void serviceHand(uint32_t now){
    serviceTimerTest(now);
    if(irTestMode){
      if(timerTestMode&&(timerTestStage==TimerTestStage::Waiting||timerTestStage==TimerTestStage::Completed)){resetHandGesture();return;}
      if(!storageOk||!stepper.verified){pauseSchedule("IR tests stopped: check storage and stepper setup.");return;}
      if(!handGestureReady(now))return;
      // Test motion has no daily dose journal entry. Entry already invalidated any pending dose.
      targetSlot=positionKnown?uint8_t(currentSlot%SlotCount+1):0;
      stepsRemaining=stepper.stepsPerSlot;moveDirection=stepper.direction;
      motion=Motion::IrTest;phase=Phase::Queued;positionKnown=false;token[0]=0;invalidateIr();
      lastAction="IR test: rotating one compartment. This is not a recorded daily dose.";return;
    }
    if(!scheduleEnabled||!clockSynced||!weekConfigured()||weekRefillDue()||localDay()<journal.maxDay||localMinute()<schedule.dueMinute){resetHandGesture();return;}const DailyRecord *record=recordFor(localDay());
    if(stateOf(record)!=DayState::Ready||record->slot!=calendarSlot()||!positionKnown||currentSlot%SlotCount+1!=record->slot||!stepper.verified){resetHandGesture();return;}
    if(!handGestureReady(now))return;
    const uint8_t selected=record->slot;const uint32_t day=record->day;
    if(!changeRecord(day,DayState::Attempted))return; // Durable lockout BEFORE energizing any stepper coil.
    targetSlot=selected;motionDay=day;stepsRemaining=stepper.stepsPerSlot;moveDirection=stepper.direction;
    motion=Motion::Dose;phase=Phase::Queued;positionKnown=false;token[0]=0;invalidateIr();
    lastAction="Hand accepted. Today's attempt is recorded; rotating one compartment to release the pills.";
  }

  void servicePillboxSensors(uint32_t now){
    // Never delay a motor step or an already timed active hand hold for DHT.
    const bool canBlock=phase==Phase::Idle&&(!handArmed||irRaw!=IrHandLevel);
    if(!sensors.sample(now,canBlock))return;
    temperature=sensors.temperatureX100;humidity=sensors.rhX100;
    environmentFlags=sensors.flags;environmentAt=sensors.sampledAt;environmentSeen=true;
    invalidateIr(); // The DHT sample left a short gap in IR observation.
    Serial.printf("PB SENSORS NTC_mV=%u,temp_x100=%ld,rh_x100=%ld,DHT_temp_x100=%ld,flags=%u\n",
      unsigned(sensors.ntcMillivolts),static_cast<long>(temperature),static_cast<long>(humidity),
      static_cast<long>(sensors.dhtDiagnosticX100),unsigned(environmentFlags));
  }
  const char *providerBoxState()const{
    if(irTestMode)return timerTestMode&&timerTestStage==TimerTestStage::Overdue?"TEST_OVERDUE":"TEST";
    if(!clockSynced)return "CLOCK";
    if(!weekConfigured())return "SETUP";
    const DayState s=stateOf(recordFor(localDay()));
    if(s==DayState::None&&!scheduleEnabled)return "PAUSED";
    return dayStateName(s);
  }
  void serviceProviderReport(uint32_t now){
    if(!msp.online()){providerReportPart=0;return;}
    if(!providerReportPart&&!elapsed(now,providerReportAt,5000))return;
    char body[100];
    if(!providerReportPart){
      const bool fresh=environmentSeen&&!elapsed(now,environmentAt,16000);
      snprintf(body,sizeof(body),"PV1,0,ENV,%ld,%ld,%u",static_cast<long>(fresh?temperature:-32768),
        static_cast<long>(fresh?humidity:65535),unsigned(fresh?environmentFlags:4));
      if(uartSend(body))providerReportPart=1;
    }else{
      bool overdue=false;
      for(uint8_t i=0;i<SlotCount;++i){const DailyRecord &r=journal.records[i];
        if(r.day&&r.overdue&&stateOf(&r)!=DayState::Completed)overdue=true;
      }
      snprintf(body,sizeof(body),"PV1,0,BOX,%u,%s,%u,%u",unsigned(weekRefillDue()?7:weekDay()),
        providerBoxState(),unsigned(overdue),unsigned(weekRefillDue()));
      if(uartSend(body)){providerReportPart=0;providerReportAt=now;}
    }
  }
  void saveRefillInstructions(){
    if(!setupRequest())return;
    RefillTextSettings updated{};updated.magic=RefillTextMagic;
    for(uint8_t i=0;i<7;++i){
      char key[8];snprintf(key,sizeof(key),"day%u",unsigned(i+1));const String value=web.arg(key);
      if(value.length()>48||strlen(value.c_str())!=value.length()||!validRefillText(value.c_str())){
        web.send(400,"text/plain","Use up to 48 printable ASCII characters per day, without double quotes or backslashes.");return;
      }
      memcpy(updated.text[i],value.c_str(),value.length()+1);
    }
    if(!consumeToken())return;
    if(!saveBytes("refillText1",&updated,sizeof(updated))){requireStorage();return;}
    refillText=updated;
    web.send(200,"text/plain","Refill instructions saved for Days 1-7. Retap the pillbox card to load the updated LCD guide. Daily times and pack dates are unchanged.");
  }
  void handleUart(char *line){
    char *f[8];const size_t n=pv_split(line,f,8);uint16_t seq;if(n<3||!pv_u16(f[1],&seq)||!seq)return;
    const char *answer="ACK,ERR,BAD_COMMAND";char refill[116];
    if(n==6&&!strcmp(f[2],"ENV"))answer="ACK,ERR,LOCAL_SENSORS_ONLY";
    else if(n==4&&!strcmp(f[2],"MED")){
      uint16_t day=0;
      if(pv_u16(f[3],&day)&&day>=1&&day<=7){
        const char *text=refillText.text[day-1][0]?refillText.text[day-1]:"Not set";
        size_t used=size_t(snprintf(refill,sizeof(refill),"MED,%u,",unsigned(day)));
        static const char hex[]="0123456789ABCDEF";
        for(size_t i=0;text[i]&&used+2<sizeof(refill);++i){
          const uint8_t c=uint8_t(text[i]);refill[used++]=hex[c>>4];refill[used++]=hex[c&15];
        }
        refill[used]=0;answer=refill;
      }
    }
    else if(n==4&&!strcmp(f[2],"READER")){
      if(!strcmp(f[3],"BOOT")){readerSeen=false;answer="ACK,OK,READER_BOOT";}
      else if(!strcmp(f[3],"ONLINE")||!strcmp(f[3],"EMPTY")){readerSeen=true;readerAt=millis();answer="ACK,OK,READER_ONLINE";}
    }else if(n==4&&pv_uid_valid(f[3])&&(!strcmp(f[2],"TAG")||!strcmp(f[2],"LOGREQ"))){
      memcpy(lastUid,f[3],strlen(f[3])+1);updateWeek();
      const char *status=weekRefillDue()?"REFILL":!weekConfigured()?"SETUP":!clockSynced?"CLOCK":"ACTIVE";
      snprintf(refill,sizeof(refill),"WEEK,%u,%s",unsigned(weekRefillDue()?7:weekDay()),status);answer=refill;
      nfcLastAction="Weekly status sent; the healthcare MSP430 fetches all seven saved refill instructions. Card taps never move the motor or confirm refill.";
    }else if(n==4&&!strcmp(f[2],"CLEAR")&&pv_uid_valid(f[3]))answer="ACK,OK,CARD_REMOVED";
    char body[PV_FRAME_SIZE];snprintf(body,sizeof(body),"PV1,%u,%s",unsigned(seq),answer);uartSend(body);
  }
  void pollUart(){
    static char line[PV_FRAME_SIZE];static size_t used=0;static bool overflow=false;
    static uint32_t lastGeneration=0;
    msp.poll();
    // Never combine a partial old stream with a newly authenticated session.
    if(lastGeneration!=msp.generation()){used=0;overflow=false;lastGeneration=msp.generation();readerSeen=false;providerReportAt=millis()-5000;providerReportPart=0;}
    if(!msp.online()){used=0;overflow=false;readerSeen=false;return;}
    for(unsigned i=0;i<128&&msp.available();++i){const int c=msp.read();if(c=='\r')continue;
      if(c=='\n'){if(used&&!overflow){line[used]=0;handleUart(line);}used=0;overflow=false;}
      else if(!overflow&&used<sizeof(line)-1&&c>=32&&c<=126)line[used++]=char(c);else overflow=true;
    }
  }
  void noCache(){web.sendHeader("Cache-Control","no-store");}
  bool authorize(){if(web.authenticate("demo",accessPassword))return true;web.requestAuthentication(BASIC_AUTH,"PharmaVault");return false;}
  const char *activeToken(uint32_t now){if(phase!=Phase::Idle)return "";if(!token[0]||elapsed(now,tokenAt,TicketLifetimeMs)){randomHex(token);tokenAt=now;}return token;}
  bool consumeToken(){if(phase!=Phase::Idle||!token[0]||elapsed(millis(),tokenAt,TicketLifetimeMs)||web.arg("token")!=token){web.send(409,"text/plain","Someone else just made a change, or this page sat open too long. Check the screen and try again.");return false;}token[0]=0;return true;}
  bool requireStorage(){if(storageOk)return true;web.send(503,"text/plain","Settings storage unavailable. Automatic operation is stopped.");return false;}
  bool confirmed(){if(web.arg("confirm")=="1")return true;web.send(400,"text/plain","Confirm this setup change.");return false;}
  bool setupRequest(){if(!authorize())return false;noCache();return requireStorage()&&confirmed();}
  static void appendQuoted(String &json,const char *s){json+='"';json+=s;json+='"';} // All variable labels are validated against quotes/control/backslash.
  void appendRecord(String &json,const DailyRecord *r,uint32_t day){
    char data[220];snprintf(data,sizeof(data),"{\"day\":%lu,\"slot\":%u,\"label\":\"%s\",\"status\":\"%s\",\"overdue\":%s,\"preparedAt\":%lu,\"completedAt\":%lu}",
      static_cast<unsigned long>(r?r->day:day),r?unsigned(r->slot):0,r?r->label:"",dayStateName(stateOf(r)),r&&r->overdue?"true":"false",
      static_cast<unsigned long>(r?r->preparedAt:0),static_cast<unsigned long>(r?r->completedAt:0));json+=data;
  }
  void sendState(){
    if(!authorize())return;serviceClock(millis());updateDeadlines();updateWeek();const uint32_t now=millis();serviceTimerTest(now);const bool envFresh=environmentSeen&&!elapsed(now,environmentAt,16000);
    char temp[16]="null",rh[16]="null",epoch[24]="null",raw[8]="null",age[16]="null";
    if(envFresh&&temperature!=-32768)snprintf(temp,sizeof(temp),"%ld",static_cast<long>(temperature));
    if(envFresh&&humidity!=65535)snprintf(rh,sizeof(rh),"%ld",static_cast<long>(humidity));
    if(clockSynced)snprintf(epoch,sizeof(epoch),"%llu",static_cast<unsigned long long>(clockEpoch));
    if(phase==Phase::Idle&&irFresh(now))snprintf(raw,sizeof(raw),"%d",int(irRaw));
    if(irHasSample)snprintf(age,sizeof(age),"%lu",static_cast<unsigned long>(now-irAt));
    const bool stable=irStable(now);const String lan=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");
    snprintf(statusChunk,sizeof(statusChunk),
      "{\"token\":\"%s\",\"storageOk\":%s,\"canTest\":%s,"
      "\"linkMode\":\"wireless\",\"bridgeOnline\":%s,"
      "\"temp\":%s,\"rh\":%s,\"environment\":\"%s\",\"tag\":\"%s\",\"lanIp\":\"%s\",\"epoch\":%s,\"clockSynced\":%s,\"timezone\":\"Asia/Singapore\","
      "\"scheduleEnabled\":%s,\"dueMinute\":%u,\"alertMinute\":%u,\"canEnableSchedule\":%s,\"systemStatus\":\"%s\",\"handHoldMs\":%u,"
      "\"positionKnown\":%s,\"currentSlot\":%u,\"stepsPerSlot\":%u,\"stepperDirection\":%d,\"stepperVerified\":%s,\"motionActive\":%s,\"motionText\":\"%s\",",
      activeToken(now),storageOk?"true":"false",canTest(now)?"true":"false",msp.online()?"true":"false",
      temp,rh,!envFresh?"Waiting for local pillbox sensor readings":environmentFlags?"Environment warning or sensor fault":"Within configured demo storage limits",lastUid,lan.c_str(),epoch,clockSynced?"true":"false",
      scheduleEnabled?"true":"false",unsigned(schedule.dueMinute),unsigned(schedule.alertMinute),readyToEnable(now)?"true":"false",systemStatus(now),unsigned(HandHoldMs),
      positionKnown?"true":"false",unsigned(currentSlot),unsigned(stepper.stepsPerSlot),int(stepper.direction),stepper.verified?"true":"false",phase!=Phase::Idle?"true":"false",lastAction);
    String json(statusChunk);json.reserve(7500);
    snprintf(statusChunk,sizeof(statusChunk),
      "\"weekConfigured\":%s,\"weekStartDay\":%lu,\"weekDay\":%u,\"weekRefillDue\":%s,\"weekStatus\":\"%s\",\"canRefillWeek\":%s,\"weekNextStartDay\":%lu,",
      weekConfigured()?"true":"false",static_cast<unsigned long>(week.firstDay),unsigned(weekRefillDue()?7:weekDay()),weekRefillDue()?"true":"false",weekStatus(),canRefillWeek(now)?"true":"false",static_cast<unsigned long>(weekNextStartDay()));json+=statusChunk;
    snprintf(statusChunk,sizeof(statusChunk),
      "\"irTestMode\":%s,\"canEnableIrTest\":%s,\"irTestCycles\":%lu,\"irTestStatus\":\"%s\","
      "\"timerTestMode\":%s,\"timerTestPhase\":\"%s\",\"timerTestDueIn\":%lu,\"timerTestOverdueIn\":%lu,",
      irTestMode?"true":"false",canEnableIrTest(now)?"true":"false",static_cast<unsigned long>(irTestCycles),irTestStatus(now),
      timerTestMode?"true":"false",timerTestPhase(),static_cast<unsigned long>(timerTestSecondsUntil(now,TimerTestDueMs)),
      static_cast<unsigned long>(timerTestSecondsUntil(now,TimerTestOverdueMs)));json+=statusChunk;
    snprintf(statusChunk,sizeof(statusChunk),
      "\"irRaw\":%s,\"irStable\":%s,\"irAgeMs\":%s,\"irStableMs\":%lu,\"irActiveLevel\":%u,\"irHandDetected\":%s,\"irStatus\":\"%s\","
      "\"handArmed\":%s,\"handStatus\":\"%s\",\"nfcStatus\":\"%s\",\"nfcLastAction\":\"%s\",",
      raw,stable?"true":"false",age,static_cast<unsigned long>(irHasSample?now-irCandidateAt:0),unsigned(IrHandLevel),stable?(irRaw==IrHandLevel?"true":"false"):"null",
      irStatus(now),handArmed?"true":"false",handStatus(now),nfcStatus(now),nfcLastAction);json+=statusChunk;
    json+="\"refillInstructions\":[";
    for(uint8_t i=0;i<7;++i){if(i)json+=',';appendQuoted(json,refillText.text[i]);}
    json+="],\"today\":";appendRecord(json,recordFor(localDay()),localDay());json+=",\"history\":[";
    bool comma=false;for(uint8_t i=0;i<8;++i)if(journal.records[i].day){if(comma)json+=',';appendRecord(json,&journal.records[i],0);comma=true;}
    json+="],\"alerts\":[";comma=false;for(uint8_t i=0;i<8;++i){const DailyRecord &r=journal.records[i];if(r.day&&r.overdue&&r.state!=uint8_t(DayState::Completed)){if(comma)json+=',';appendRecord(json,&r,0);comma=true;}}
    json+="]}";noCache();web.send(200,"application/json",json);
  }

  void setIrTestMode(){
    if(!setupRequest())return;const String enabled=web.arg("enabled");
    if(enabled!="0"&&enabled!="1"){web.send(400,"text/plain","Choose enabled 0 or 1.");return;}
    const String timedArg=web.arg("timed");
    if(timedArg!=""&&timedArg!="0"&&timedArg!="1"){web.send(400,"text/plain","Choose timed 0 or 1.");return;}
    const bool timed=timedArg=="1";
    if(enabled=="1"){
      if(web.arg("unloaded")!="1"){web.send(400,"text/plain","Confirm an empty carousel or dummy contents for repeated tests.");return;}
      if(!canEnableIrTest(millis())&&!(timed&&timerTestMode&&stepper.verified&&canTest(millis()))){web.send(409,"text/plain","Save verified stepper settings, resolve faults and wait for idle cooldown before starting tests.");return;}
    }
    if(!consumeToken())return;
    if(enabled=="0"){
      pauseSchedule("IR test mode stopped. Daily schedule stays paused; confirm alignment before normal use.");invalidateIr();
      web.send(200,"text/plain",lastAction);return;
    }
    pauseSchedule("Starting IR tests. Daily schedule paused.");
    // Moving the carousel for tests must not leave a prepared daily dose resumable.
    if(!invalidatePending()){requireStorage();return;}
    irTestCycles=0;irTestMode=true;timerTestMode=timed;timerTestStartedAt=millis();timerTestStage=TimerTestStage::Waiting;invalidateIr();
    lastAction=timed?"Timer test started: ready after 10 seconds; overdue after 30 seconds. One hand-triggered test cycle, then restart to repeat.":
      "IR TEST MODE active. Clear the sensor, then hold a hand for 1.5 seconds. Tests do not record daily doses.";
    web.send(200,"text/plain",lastAction);
  }
  void setClock(){
    if(!setupRequest())return;const String value=web.arg("epoch");uint64_t parsed=0;
    if(!value.length()||value.length()>10){web.send(400,"text/plain","Send a valid UTC epoch time.");return;}
    for(size_t i=0;i<value.length();++i){const char c=value.c_str()[i];if(c<'0'||c>'9'){web.send(400,"text/plain","Invalid epoch time.");return;}parsed=parsed*10+unsigned(c-'0');}
    if(parsed<1577836800ULL||parsed>=4102444800ULL){web.send(400,"text/plain","Clock date must be between 2020 and 2099.");return;}
    if(!consumeToken())return;pauseSchedule("Clock changed. Check the displayed Singapore time and re-enable the scheduler.");
    clockEpoch=parsed;clockTick=millis();clockRemainder=0;clockSynced=true;updateDeadlines();updateWeek();
    web.send(200,"text/plain","Clock set from the laptop. Verify Singapore date/time before enabling.");
  }
  void saveSchedule(){
    if(!setupRequest())return;uint16_t due=0,alert=0;
    if(!pv_u16(web.arg("dueMinute").c_str(),&due)||!pv_u16(web.arg("alertMinute").c_str(),&alert)||due>=alert||alert>1439){web.send(400,"text/plain","Choose a daily time and a later alert time on the same day.");return;}
    if(!consumeToken())return;pauseSchedule("Schedule settings changed. They do not reset today's recorded attempt.");
    const ScheduleSettings updated{ScheduleMagic,due,alert};if(!saveBytes("schedule1",&updated,sizeof(updated))){requireStorage();return;}schedule=updated;updateDeadlines();updateWeek();web.send(200,"text/plain","Daily and alert times saved in Singapore time. These repeat without daily changes. Re-enable when ready.");
  }
  void setScheduleMode(){
    if(!setupRequest())return;const String enabled=web.arg("enabled");if(enabled!="0"&&enabled!="1"){web.send(400,"text/plain","Choose enabled 0 or 1.");return;}
    if(enabled=="1"&&irTestMode){web.send(409,"text/plain","Stop IR test mode and confirm alignment before enabling the daily schedule.");return;}
    if(enabled=="1"&&!readyToEnable(millis())){web.send(409,"text/plain",systemStatus(millis()));return;}
    if(!consumeToken())return;if(enabled=="0")pauseSchedule("Daily schedule and IR tests paused.");else scheduleEnabled=true;invalidateIr();
    lastAction=scheduleEnabled?"Scheduler enabled. At the daily time today's dose becomes available; only a fresh hand presentation moves the carousel.":"Scheduler paused. Existing history and alerts remain saved.";web.send(200,"text/plain",lastAction);
  }
  void saveStepper(){
    if(!setupRequest())return;uint16_t count=0;int32_t direction=0;
    if(!pv_u16(web.arg("stepsPerSlot").c_str(),&count)||!count||count>4096||!pv_i32(web.arg("direction").c_str(),&direction)||(direction!=1&&direction!=-1)||web.arg("verified")!="1"){web.send(400,"text/plain","Use 1-4096 half-steps, direction 1 or -1, and confirm the observed one-compartment travel.");return;}
    if(!consumeToken())return;pauseSchedule("Stepper setup changed. Confirm the current compartment again.");if(!invalidatePending()){requireStorage();return;}
    const StepperSettings updated{StepperMagic,count,int8_t(direction),1};if(!saveBytes("stepper1",&updated,sizeof(updated))){requireStorage();return;}stepper=updated;positionKnown=false;web.send(200,"text/plain","Stepper setup saved. Confirm the actual current compartment before enabling.");
  }
  void jogStepper(){
    if(!setupRequest())return;int32_t count=0;
    if(!canTest(millis())){web.send(409,"text/plain","Wait between movements and resolve any storage fault.");return;}
    if(!pv_i32(web.arg("steps").c_str(),&count)||!count||count<-512||count>512||web.arg("clear")!="1"){web.send(400,"text/plain","Use -512 to 512 half-steps, excluding zero, with the mechanism clear.");return;}
    if(!consumeToken())return;pauseSchedule("Manual stepper jog. Confirm alignment again afterward.");if(!invalidatePending()){requireStorage();return;}
    positionKnown=false;moveDirection=count<0?-1:1;stepsRemaining=uint16_t(count<0?-count:count);targetSlot=0;motion=Motion::Jog;phase=Phase::Queued;invalidateIr();
    web.send(202,"text/plain","Jogging the stepper. Movement can release contents; observe the unloaded mechanism.");
  }
  void alignStepper(){
    if(!setupRequest())return;uint16_t slot=0;if(!pv_u16(web.arg("slot").c_str(),&slot)||slot<1||slot>8||web.arg("verified")!="1"||web.arg("handled")!="1"){web.send(400,"text/plain","Confirm the actual current compartment and that missed doses were removed before realignment.");return;}
    // Also enforce this before the clock is set: a Ready day reserves the NEXT physical compartment.
    for(uint8_t i=0;i<SlotCount;++i){const DailyRecord &r=journal.records[i];
      if(r.day&&r.state==uint8_t(DayState::Ready)&&slot%SlotCount+1!=r.slot){web.send(409,"text/plain","Pending dose requires the current compartment immediately before its target. Inspect alignment before resuming.");return;}
    }
    if(!consumeToken())return;pauseSchedule("Current compartment confirmed. Check time and enable when ready.");currentSlot=uint8_t(slot);positionKnown=true;web.send(200,"text/plain","Current position recorded without movement. After the daily time, a hand advances to the NEXT compartment.");
  }
  void refillWeek(){
    if(!setupRequest())return;serviceClock(millis());updateDeadlines();updateWeek();
    if(web.arg("filled")!="1"||web.arg("parked")!="1"){web.send(400,"text/plain","Confirm days 1-7 are refilled with the correct day-specific pills and compartment 8 is empty and physically parked.");return;}
    if(!canRefillWeek(millis())){web.send(409,"text/plain","A new week needs the refill reminder (or initial setup), a correct clock, verified stepper and idle cooldown. Stop IR tests first.");return;}
    if(!consumeToken())return;pauseSchedule("Recording a new seven-day pack.");
    // Preserve lockouts, including any attempt already recorded on this calendar date.
    if(!invalidatePending()){requireStorage();return;}
    const WeekSettings updated{WeekMagic,weekNextStartDay(),uint32_t(clockEpoch),0,{0,0,0}};
    if(!saveBytes("week1",&updated,sizeof(updated))){requireStorage();return;}
    week=updated;currentSlot=8;positionKnown=true;scheduleEnabled=true;invalidateIr();
    lastAction="All seven days recorded; empty compartment 8 confirmed. Daily timer enabled with the same saved times. No NFC tap needed.";
    web.send(200,"text/plain",lastAction);
  }
  void saveWifi(){
    if(!authorize())return;noCache();if(!requireStorage())return;const String ssid=web.arg("ssid"),password=web.arg("password");
    if(ssid.length()<1||ssid.length()>32||password.length()>63||(password.length()&&password.length()<8)||strlen(ssid.c_str())!=ssid.length()||strlen(password.c_str())!=password.length()){web.send(400,"text/plain","Use a valid personal 2.4 GHz network name and password.");return;}
    if(!consumeToken())return;pauseSchedule("Wi-Fi changed. Re-enable the scheduler when ready.");NetworkSettings updated{};updated.magic=NetworkMagic;memcpy(updated.ssid,ssid.c_str(),ssid.length()+1);memcpy(updated.password,password.c_str(),password.length()+1);
    if(!saveBytes("network",&updated,sizeof(updated))){requireStorage();return;}network=updated;networkQueued=true;web.send(200,"text/plain","Wi-Fi saved. The ESP32 access point remains available.");
  }
  void startWeb(){
    web.on("/",HTTP_GET,[this]{if(!authorize())return;noCache();web.sendHeader("Content-Encoding","gzip");web.send_P(200,"text/html; charset=utf-8",reinterpret_cast<const char*>(PvDashboardGz),sizeof(PvDashboardGz));});web.on("/state",HTTP_GET,[this]{sendState();});web.on("/mascot.webp",HTTP_GET,[this]{if(!authorize())return;web.sendHeader("Cache-Control","private, max-age=86400");web.send_P(200,"image/webp",reinterpret_cast<const char*>(PvMascotWebp),sizeof(PvMascotWebp));});
    web.on("/test/mode",HTTP_POST,[this]{setIrTestMode();});
    web.on("/clock",HTTP_POST,[this]{setClock();});web.on("/schedule/save",HTTP_POST,[this]{saveSchedule();});web.on("/schedule/mode",HTTP_POST,[this]{setScheduleMode();});
    web.on("/stepper/save",HTTP_POST,[this]{saveStepper();});web.on("/stepper/jog",HTTP_POST,[this]{jogStepper();});web.on("/stepper/align",HTTP_POST,[this]{alignStepper();});
    web.on("/refill/instructions",HTTP_POST,[this]{saveRefillInstructions();});web.on("/week/refill",HTTP_POST,[this]{refillWeek();});web.on("/wifi",HTTP_POST,[this]{saveWifi();});
    web.onNotFound([this]{if(!authorize())return;noCache();web.send(404,"text/plain","Not found");});web.begin();
  }
  void printAccess(){Serial.println();Serial.println("PHARMAVAULT PBesp wireless 2 (local sensors)");Serial.println("ULN2003 IN1/2/3/4 = GPIO5/6/7/8; GPIO3 unused; IR OUT=GPIO14 at3.3V, LOW=hand, hold=1500ms.");Serial.println("Sensor link: FireBeetle wireless bridge on TCP 4210; S3 GPIO4/17 unused.");Serial.print("Wi-Fi name: ");Serial.println(apSsid);Serial.print("Wi-Fi password: ");Serial.println(accessPassword);Serial.println("Dashboard: http://pharmavault (or http://192.168.4.1)");Serial.println("Dashboard username: demo");Serial.print("Dashboard password: ");Serial.println(accessPassword);if(WiFi.status()==WL_CONNECTED){Serial.print("Same-Wi-Fi dashboard: http://pharmavault.local or http://");Serial.println(WiFi.localIP());}}
  void printDiagnostics(){char line[260];snprintf(line,sizeof(line),"Daily: clock=%s day=%lu minute=%u enabled=%s position=%s slot=%u IR=%d armed=%s",clockSynced?"set":"unset",static_cast<unsigned long>(localDay()),unsigned(localMinute()),scheduleEnabled?"yes":"no",positionKnown?"known":"unknown",unsigned(currentSlot),int(irRaw),handArmed?"yes":"no");Serial.println(line);Serial.println(systemStatus(millis()));Serial.println(lastAction);Serial.println(irTestStatus(millis()));Serial.println(weekStatus());}
 public:
  void begin(){
    pinMode(RetiredServo,INPUT);pinMode(Step1,OUTPUT);pinMode(Step2,OUTPUT);pinMode(Step3,OUTPUT);pinMode(Step4,OUTPUT);writeCoils(0);
    pinMode(RetiredTrigger,INPUT);pinMode(IrPin,INPUT_PULLUP);pinMode(UartRx,INPUT);pinMode(UartTx,INPUT);Serial.begin(115200);sensors.begin();
    loadSettings();WiFi.mode(WIFI_AP_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);snprintf(apSsid,sizeof(apSsid),"PharmaVault-%012llX",static_cast<unsigned long long>(ESP.getEfuseMac()&0xFFFFFFFFFFFFULL));
    const IPAddress ip(192,168,4,1),mask(255,255,255,0);apStarted=WiFi.softAPConfig(ip,ip,mask,IPAddress(uint32_t(0)),ip)&&WiFi.softAP(apSsid,accessPassword);if(apStarted)dns.start(53,"pharmavault",ip);if(MDNS.begin("pharmavault"))MDNS.addService("http","tcp",80);msp.begin(accessPassword);startWeb();if(network.magic==NetworkMagic)WiFi.begin(network.ssid,network.password);printAccess();
  }
  void run(){
    serviceClock(millis());pollUart();updateDeadlines();updateWeek();serviceTimerTest(millis());serviceProviderReport(millis());
    if(phase!=Phase::Idle){runMotion();delay(1);return;}
    pollIr(millis());servicePillboxSensors(millis());
    if(networkQueued){networkQueued=false;WiFi.disconnect(false,false);WiFi.begin(network.ssid,network.password);}
    const bool connected=WiFi.status()==WL_CONNECTED;if(connected&&!wasConnected){Serial.print("Same-Wi-Fi dashboard: http://pharmavault.local or http://");Serial.println(WiFi.localIP());}wasConnected=connected;
    if(Serial.available()){const int c=Serial.read();if(c=='?')printAccess();else if(c=='i'||c=='I'||c=='n'||c=='N'||c=='u'||c=='U')printDiagnostics();}
    web.handleClient();serviceClock(millis());serviceDaily(millis());serviceHand(millis());delay(1);
  }
};
PharmaVault cabinet;
void setup(){cabinet.begin();}
void loop(){cabinet.run();}
