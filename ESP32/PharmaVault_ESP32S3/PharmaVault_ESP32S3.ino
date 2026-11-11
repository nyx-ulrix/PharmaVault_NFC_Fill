/* PHARMAVAULT: ESP32-S3 breakout, ESP32S3 Dev Module profile. Arduino-ESP32 core 3.x.
 * This is the complete sketch: dashboard and UART protocol are included below.
 * GPIO5 SG92R carousel; GPIO3 SG90 flap; GPIO13 HC-SR04 TRIG; GPIO14 ECHO via existing resistor divider.
 * GPIO4 UART RX from MSP P3.3; GPIO17 UART TX to MSP P3.4; 9600 8N1.
 * Uses only libraries bundled with the Espressif Arduino-ESP32 board package.
 * First boot: Serial Monitor 115200 prints a private Wi-Fi name and password.
 * No source editing: join that network, open http://192.168.4.1, user demo.
 * The same password protects Wi-Fi and the dashboard. Settings persist in NVS.
 * HC ECHO -> 2.2k -> GPIO14 junction -> 3.2k -> common GND. HC VCC = 5V.
 * Sensor readings start immediately; servo endpoints are set in the browser.
 * Fill percentage estimates height in the current sensor view, not pill count.
 * NFC auto mode starts paused; enable after calibration and clear the reader.
 * Use the companion MSP430 NFC sequence 1 firmware for removal/heartbeat events.
 * A missing/invalid echo is UNKNOWN, never evidence of an empty compartment.
 * Calibrate reachable carousel positions, command positioning, verify alignment, then cycle the SG90 flap.
 */
#include <Arduino.h>
#include <WebServer.h>
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

static const char DashboardHtml[] PROGMEM = R"PVHTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PharmaVault</title><style>
:root{font:16px/1.5 system-ui,sans-serif;color:#1e3439;background:#eef4f3}body{margin:0}main{max-width:960px;margin:auto;padding:24px 16px 48px}h1{font-size:2.4rem;margin:0}h2{font-size:1.2rem;margin:0 0 12px}.brand{font-weight:700;color:#137464;letter-spacing:.12em;font-size:.8rem}.note{color:#50686c}section,details{background:white;border:1px solid #ccdcda;border-radius:12px;padding:18px;margin:16px 0}.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.card{background:#f3f8f6;border-radius:8px;padding:14px}.card strong{display:block;font-size:1.65rem}.card span{font-size:.85rem}label{display:block;margin:12px 0}input,select,button{font:inherit;padding:8px;border-radius:6px;border:1px solid #a3b9b6;box-sizing:border-box}input[type=text],input[type=password]{width:min(100%,360px)}input[type=number]{width:100px}input[type=checkbox]{margin-right:7px}button{background:#0b6d5b;color:white;border:0;font-weight:600;cursor:pointer}button:disabled{background:#809592;cursor:default}.secondary{background:#dbe9e4;color:#234c42}.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px}.status{font-weight:600}summary{font-weight:700;cursor:pointer}code{overflow-wrap:anywhere}.message{white-space:pre-wrap;min-height:1.5em}ol{padding-left:25px}small{display:block;color:#50686c}a{color:#0b6d5b}@media(max-width:560px){.cards{grid-template-columns:1fr}h1{font-size:2rem}}
</style></head><body><main>
<div class="brand">PHARMAVAULT</div><h1>Medication cabinet</h1>
<p class="note">SG92R positions the carousel · SG90 opens and closes the flap · use dummy contents during testing.</p>
<section><h2>Live readings</h2><div class="cards">
<div class="card"><span>NTC temperature</span><strong id="temp">—</strong></div>
<div class="card"><span>DHT11 humidity</span><strong id="rh">—</strong></div>
<div class="card"><span>HC-SR04 distance</span><strong id="range">—</strong></div></div>
<p id="environment" role="status">Waiting for readings…</p><small>Distance is a raw observation; it does not confirm a pill was dispensed.</small>
<p>Last RC522 tag: <code id="tag">Waiting for a scan</code></p><p id="storage" class="note"></p></section>
<section><h2>Tap a card to dispense</h2>
<p id="nfcStatus" class="status" role="status">Tap dispensing is paused.</p>
<p id="nfcLastAction" class="note">No card action yet.</p>
<p>When enabled, a newly tapped assigned card commands: <strong>close flap → rotate to its saved compartment → open flap → close flap</strong>. The motors run one after the other.</p>
<label><input id="nfcConfirm" type="checkbox">I checked the saved flap and compartment positions with this mechanism, loaded dummy contents, and cleared the moving parts.</label>
<div class="row"><button id="enableNfc" type="button" disabled>Enable tap dispensing</button><button id="pauseNfc" type="button" class="secondary" disabled>Pause tap dispensing</button></div>
<p id="nfcMessage" class="message" role="status"></p>
<small>Enabling does not move a motor. Remove all cards and wait until the status says ready before tapping; this can take up to five seconds. Holding a card does not request repeated cycles. A scan during movement or the three-second pause afterward is not queued; remove and retap when ready.</small>
<p class="note">Pause before scanning cards for assignment or calibrating the sensor. Tap dispensing starts paused after either MCU restarts and pauses after manual movements or calibration/assignment changes. Pause prevents another cycle; it is not an emergency stop. Command completion does not verify that a pill was dispensed or taken.</p>
</section>
<section><h2>Estimated compartment fill height</h2>
<div class="card"><span>Estimated fill height — current sensor view</span><strong id="fillPercent">Unavailable</strong><progress id="fillBar" max="100" value="0" hidden style="width:100%" aria-label="Estimated fill height"></progress></div>
<p id="fillStatus" class="status" role="status">Waiting for a calibrated, steady reading.</p>
<p id="rangeState" class="note">Waiting for a measurement.</p>
<small id="rangeDiagnostic">No measurements received yet.</small>
<p class="note">This estimates the height of the contents from their reflected surface. It is not a pill count or a measurement of all eight compartments. No echo, unstable readings or movement means the percentage is unavailable.</p>
<details><summary>Calibrate 0% empty and 100% full</summary>
<ol><li>Pause tap dispensing. Fix the sensor above the compartment, facing its bottom squarely.</li>
<li>Leave the compartment empty. Wait for five steady readings, tick the box below, then capture 0% empty.</li>
<li>Load representative dummy contents to your chosen full level without moving the sensor. The top surface must remain at least 20 mm from the sensor and be at least 20 mm nearer than the empty bottom. Wait for steady readings, tick the box again, then capture 100% full.</li></ol>
<p class="note">For a table demonstration, capture a flat target at 150 mm as empty and 50 mm as full. A target at 100 mm should then show about 50%. These are simulated heights; recalibrate after mounting. Small pills, uneven surfaces and compartment walls can give unreliable echoes.</p>
<p><code>Fill height = (empty distance − current distance) ÷ (empty distance − full distance) × 100%</code></p>
<p id="rangeReferences">0% empty: unset · 100% full: unset</p>
<p id="captureStatus" class="note">Waiting for steady readings.</p>
<label><input id="fillConfirm" type="checkbox">The sensor is fixed, this is the correct empty or full level, and I am ready to save the reference.</label>
<div class="row"><button id="captureEmpty" type="button" disabled>Capture 0% empty</button><button id="captureFull" type="button" disabled>Capture 100% full</button><button id="clearFill" type="button" class="secondary" disabled>Clear fill calibration</button></div>
<p id="fillMessage" class="message" role="status"></p>
<small>Capturing empty clears the old full reference. References survive restart and apply only to this mounting and compartment geometry. The earlier loaded-view calibration is not used as 100% full. These controls save settings without moving a motor.</small>
</details></section>
<section><h2>Position the carousel, then operate the flap</h2><p id="gate" class="status" role="status">Connecting…</p>
<form id="releaseForm"><label>Compartment <select id="slot"><option value="">Choose</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label>
<label><input id="positionConfirm" type="checkbox">The mechanism is clear, the flap can close, and I am ready to observe carousel movement.</label>
<button id="positionButton" type="button" class="secondary" disabled>Position carousel</button>
<p id="positionMessage" class="message" role="status"></p><p id="carouselStatus" class="note">No carousel position commanded yet.</p>
<small>Position carousel first closes the SG90 flap, then moves the SG92R to this compartment's saved position. Calibrate both motors below first. This action does not open the flap.</small>
<label><input id="aligned" type="checkbox">I visually checked this compartment is aligned with the opening, the flap is closed, and dummy contents are loaded.</label>
<button id="releaseButton" disabled>Run flap cycle</button></form><p id="releaseMessage" class="message" role="status"></p>
<small>Run flap cycle opens and closes only the SG90. Verify alignment after positioning; the software has no position switch. You can also align the carousel by hand before confirming.</small></section>
<details id="servoDetails"><summary>Flap positions — SG90</summary>
<p>Both Open and Closed move the same SG90 flap motor. Testing a position drives it for 0.6 seconds.</p>
<p class="note">Begin with the horn unlinked. Start near 1500 µs, then adjust in small steps without hitting a mechanical stop. Recheck with the final linkage. The flap must return closed when servo power is removed.</p>
<label><input id="testConfirm" type="checkbox">The flap mechanism is clear and I am ready to observe a movement.</label>
<div class="row"><label>Closed <input id="closedUs" type="number" min="900" max="2100" step="10" value="1500"> µs</label><button class="secondary testButton" data-role="closed" type="button" disabled>Test closed position</button></div>
<div class="row"><label>Open <input id="openUs" type="number" min="900" max="2100" step="10" value="1500"> µs</label><button class="secondary testButton" data-role="open" type="button" disabled>Test open position</button></div>
<small>After every test, wait at least 3 seconds after movement finishes and tick the mechanism-clear box again. The box clears automatically.</small>
<p id="testStatus" class="note">No flap positions tested yet.</p>
<label><input id="saveConfirm" type="checkbox">I tested both displayed flap positions with this linkage, checked travel, and verified the flap returns closed with power removed.</label>
<button id="saveServo" type="button" disabled>Save flap positions</button>
<button id="disableServo" type="button" class="secondary" disabled>Disable flap</button>
<p id="servoMessage" class="message" role="status"></p><small>Starting another flap test disables normal flap cycles until both tested positions are saved again. Saved positions survive restart.</small></details>
<details id="carouselDetails"><summary>Carousel positions — SG92R</summary>
<p>Test and save a separate position for each reachable compartment. A test moves only the SG92R for about 1 second. Begin with the horn unlinked and the mechanism unloaded.</p>
<p class="note">The SG92R has limited travel. Do not force a full turn or assume it can reach all compartments. Save only positions verified on your mechanism; unused or unreachable compartments can remain unset.</p>
<label>Compartment to calibrate <select id="carouselSlot"><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label>
<label>Carousel position <input id="carouselUs" type="number" min="900" max="2100" step="10" value="1500"> µs</label>
<label><input id="carouselTestConfirm" type="checkbox">The mechanism is unloaded and clear, the flap will not obstruct it, and I am ready to observe movement.</label>
<button id="testCarousel" type="button" class="secondary" disabled>Test carousel position</button>
<small>Testing clears this compartment's previous saved position. Wait at least 3 seconds after movement finishes and tick the test confirmation again before another test.</small>
<p id="carouselTestStatus" class="note">No carousel position tested yet.</p>
<label><input id="carouselSaveConfirm" type="checkbox">I tested this displayed position and visually verified this compartment is aligned without forcing the mechanism.</label>
<button id="saveCarousel" type="button" disabled>Save carousel position</button>
<p id="carouselMessage" class="message" role="status"></p>
<p>Saved compartment positions:</p><ol id="carouselMappings"></ol>
<small>Only the selected compartment's position is saved. Saved positions survive restart; physical alignment must still be checked after movement.</small></details>
<details><summary>Connect to your Wi-Fi</summary>
<p id="networkStatus">ESP32 access point is available at 192.168.4.1.</p>
<form id="wifiForm"><label>Wi-Fi name <input id="ssid" type="text" maxlength="32" autocomplete="off" required></label>
<label>Wi-Fi password <input id="wifiPassword" type="password" maxlength="63" autocomplete="new-password"></label>
<small>Use a 2.4 GHz personal Wi-Fi network. Leave the password empty only for an open network. Campus networks requiring a web login or enterprise login are not supported here.</small>
<p><button id="joinWifi" disabled>Save and connect</button></p></form><p id="wifiMessage" class="message" role="status"></p>
<small>Your credentials stay on this ESP32. Its own access point remains available if the network cannot be reached.</small></details>
<details><summary>Assign NFC tags to compartments</summary><p>Pause tap dispensing before scanning a card for assignment. Choose its compartment, then save the UID. With tap dispensing enabled, a new tap on an assigned card runs that compartment's calibrated carousel-and-flap sequence. Unassigned cards do not move a motor.</p>
<form id="tagForm"><label>Compartment <select id="tagSlot"><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label>
<label>Card UID <input id="uid" type="text" maxlength="20" autocomplete="off" placeholder="Scan a tag or enter its UID"></label>
<div class="row"><button type="button" id="useTag" class="secondary">Use last scanned tag</button><button id="saveTag" disabled>Save assignment</button></div><small>Submit an empty UID to clear this compartment's assignment.</small></form>
<p id="tagMessage" class="message" role="status"></p><ol id="mappings"></ol></details>
<section><h2>Last flap command per compartment</h2><ol id="log"></ol><small>These command logs reset on restart. “PWM sent” records a command, not verified dispensing.</small></section>
</main><script>
'use strict';
const el = id => document.getElementById(id);
let state = null, token = '', busy = false, firstState = true;
let rangeExpiryTimer = null, rangeDisplayFresh = false;
function controls() {
  const available = Boolean(token) && !busy;
  const selected = Number(el('slot').value);
  const saved = state?.carouselPositions?.[selected-1] || 0;
  const testedCarousel = state?.carouselTestSlot === Number(el('carouselSlot').value) &&
    state?.carouselTestPulse === Number(el('carouselUs').value) && state?.carouselTestPulse > 0;
  const testedFlap = state?.testOpen === Number(el('openUs').value) &&
    state?.testClosed === Number(el('closedUs').value) && state?.testOpen > 0 &&
    state?.testClosed > 0 && state?.testOpen !== state?.testClosed;
  el('releaseButton').disabled = !available || !state?.canRelease || !selected || !el('aligned').checked;
  el('positionButton').disabled = !available || !state?.canCarouselMove || !saved || !el('positionConfirm').checked;
  document.querySelectorAll('.testButton').forEach(b => b.disabled = !available || !state?.canTest || !el('testConfirm').checked);
  el('saveServo').disabled = !available || !state?.storageOk || !state?.canTest || !testedFlap || !el('saveConfirm').checked;
  el('disableServo').disabled = !available || !state?.storageOk;
  el('testCarousel').disabled = !available || !state?.canTest || !el('carouselTestConfirm').checked;
  el('saveCarousel').disabled = !available || !state?.storageOk || !state?.canTest || !testedCarousel || !el('carouselSaveConfirm').checked;
  el('joinWifi').disabled = !available || !state?.storageOk;
  el('saveTag').disabled = !available || !state?.storageOk || Boolean(state?.nfcEnabled);
  el('useTag').disabled = Boolean(state?.nfcEnabled);
  el('enableNfc').disabled = !available || !state?.canEnableNfc || Boolean(state?.nfcEnabled) || !el('nfcConfirm').checked;
  el('pauseNfc').disabled = !available || !state?.nfcEnabled;
  const capture = available && state?.canCaptureFill && !state?.nfcEnabled && !state?.motionActive && rangeDisplayFresh && el('fillConfirm').checked;
  el('captureEmpty').disabled = !capture;
  el('captureFull').disabled = !capture || !state?.fillEmptyMm;
  el('clearFill').disabled = !available || !state?.storageOk || Boolean(state?.nfcEnabled) || !(state?.fillEmptyMm || state?.fillFullMm);
}
function clearMovementConfirmations() {
  ['aligned','positionConfirm','testConfirm','carouselTestConfirm','saveConfirm','carouselSaveConfirm','nfcConfirm','fillConfirm'].forEach(id => {el(id).checked=false;});
}
async function fetchTimed(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 3500);
  try { return await fetch(url, {...options, signal:controller.signal}); }
  finally { clearTimeout(timer); }
}
async function post(url, fields, target) {
  if (busy || !token) return;
  busy = true;
  if (['/release','/servo/test','/carousel/test','/carousel/move'].includes(url)) invalidateRange('Movement requested. Waiting for five fresh readings afterward.');
  const usedToken = token; token = ''; controls();
  el(target).textContent = 'Sending…';
  try {
    const response = await fetchTimed(url, {method:'POST',body:new URLSearchParams({...fields, token:usedToken})});
    el(target).textContent = await response.text();
  } catch {
    el(target).textContent = target === 'fillMessage' ? 'Response lost. Check the displayed references before capturing again.' : target === 'nfcMessage' ? 'Response lost. Check the displayed tap-dispensing mode before scanning a card.' : 'Response lost. Check the mechanism and displayed status before sending another request.';
  } finally { busy = false; controls(); }
}
function list(id, values) {
  el(id).replaceChildren(...values.map(value => {const li = document.createElement('li');li.textContent = value;return li;}));
}
function selectCarouselCalibration() {
  const saved = state?.carouselPositions?.[Number(el('carouselSlot').value)-1] || 0;
  el('carouselUs').value = saved || 1500;
  clearMovementConfirmations(); controls();
}
function invalidateRange(message) {
  if (rangeExpiryTimer !== null) clearTimeout(rangeExpiryTimer);
  rangeExpiryTimer = null; rangeDisplayFresh = false;
  el('range').textContent = 'Unavailable';
  el('fillPercent').textContent = 'Unavailable';
  el('fillBar').hidden = true;
  el('fillStatus').textContent = message;
  controls();
}
function renderRange() {
  if (rangeExpiryTimer !== null) clearTimeout(rangeExpiryTimer);
  const age = state.rangeAgeMs;
  const fresh = age !== null && Number.isFinite(age) && age >= 0 && age <= 2000;
  rangeDisplayFresh = fresh && !state.motionActive && state.rangeStatus === 'Valid';
  el('range').textContent = state.mm !== null && rangeDisplayFresh ? state.mm+' mm' : 'Unavailable';
  const percent = state.fillPercent;
  const usable = rangeDisplayFresh && state.rangeStable && Number.isFinite(percent) && percent >= 0 && percent <= 100;
  el('fillPercent').textContent = usable ? percent+'%' : 'Unavailable';
  el('fillBar').hidden = !usable;
  el('fillBar').value = usable ? percent : 0;
  el('fillStatus').textContent = state.motionActive ? 'Movement in progress. Waiting for fresh readings afterward.' : state.fillStatus || 'Capture empty and full references for the current sensor view.';
  const statusHelp = {
    Waiting:'Waiting for the first measurement.',
    Valid:'A valid distance was received.',
    IdleHigh:'ECHO is already high before a trigger. Check the 3.3V-safe ECHO path to GPIO14 and the shared ground.',
    NoPulse:'No complete echo pulse. Aim at a broad flat target 150 mm away, then check 5V power, TRIG GPIO13, the 3.3V-safe ECHO path to GPIO14, and shared ground.',
    OutOfRange:'Echo received, but the calculated distance is outside this firmware’s 20–3000 mm range.',
    Moving:'Measurements are paused during a motor command. Waiting for fresh readings after movement.'
  };
  el('rangeState').textContent = statusHelp[state.motionActive ? 'Moving' : state.rangeStatus] || String(state.rangeStatus || 'Waiting for the sensor.');
  const measuredAge = age === null || !Number.isFinite(age) ? 'not sampled' : age+' ms ago';
  const spread = state.rangeSamples ? state.rangeSpread+' mm' : '—';
  el('rangeDiagnostic').textContent = 'Echo: '+(state.echoUs || 0)+' µs · Raw distance: '+(state.rangeRawMm || '—')+' mm · Steady samples: '+(state.rangeSamples || 0)+'/5 · Spread: '+spread+' · Last attempt: '+measuredAge;
  el('rangeReferences').textContent = '0% empty: '+(state.fillEmptyMm ? state.fillEmptyMm+' mm' : 'unset')+' · 100% full: '+(state.fillFullMm ? state.fillFullMm+' mm' : 'unset');
  el('captureStatus').textContent = state.nfcEnabled ? 'Pause tap dispensing before calibrating the sensor.' : state.canCaptureFill && rangeDisplayFresh ? 'Ready to capture: median '+state.rangeMedianMm+' mm from five steady readings.' : 'Capture needs five consecutive valid readings within 10 mm spread, a fresh sample, settings storage, idle motors and paused tap dispensing.';
  rangeExpiryTimer = rangeDisplayFresh ? setTimeout(() => invalidateRange('Waiting for a fresh reading. The previous percentage has expired.'), 2001-age) : null;
}
async function refresh() {
  if (!busy) {
    try {
      const response = await fetchTimed('/state', {cache:'no-store'});
      if (!response.ok) throw new Error('Status unavailable');
      state = await response.json(); token = state.token;
      el('temp').textContent = state.temp === null ? 'Unavailable' : (state.temp/100).toFixed(1)+' °C';
      el('rh').textContent = state.rh === null ? 'Unavailable' : (state.rh/100).toFixed(1)+' %';
      renderRange();
      el('nfcStatus').textContent = (state.nfcEnabled ? 'Enabled — ' : 'Paused — ')+(state.nfcStatus || (state.nfcReady ? 'Reader connected.' : 'Waiting for the MSP430 reader.'));
      el('nfcLastAction').textContent = state.nfcLastAction || 'No card action yet.';
      if (state.motionActive) clearMovementConfirmations();
      el('environment').textContent = state.environment;
      el('gate').textContent = state.gate;
      el('tag').textContent = state.tag ? state.tag+(state.tagSlot?' · compartment '+state.tagSlot:' · unassigned') : 'Waiting for a scan';
      el('storage').textContent = state.storageOk ? '' : 'Settings storage is unavailable. Readings work; restart the ESP32 before saving settings or moving either motor.';
      el('networkStatus').textContent = state.lanIp ? 'Connected to your Wi-Fi. Dashboard: http://'+state.lanIp+' · Access point: http://192.168.4.1' : 'ESP32 access point: http://192.168.4.1 · Saved Wi-Fi is not connected.';
      el('testStatus').textContent = 'Flap positions tested this session: closed '+(state.testClosed||'—')+' µs, open '+(state.testOpen||'—')+' µs. '+state.testResult;
      el('carouselTestStatus').textContent = state.carouselTestSlot ? 'Carousel position tested this session: compartment '+state.carouselTestSlot+' at '+state.carouselTestPulse+' µs. '+state.carouselResult : 'No carousel position tested this session. '+state.carouselResult;
      el('carouselStatus').textContent = (state.carouselCommandedSlot ? 'Last commanded carousel position: compartment '+state.carouselCommandedSlot+'. Verify physical alignment. ' : 'No current carousel position recorded. ')+state.carouselResult;
      list('carouselMappings', state.carouselPositions.map(pulse => pulse ? pulse+' µs' : 'Not calibrated'));
      list('log', state.log); list('mappings', state.mappings.map(uid => uid||'Unassigned'));
      if (firstState) {
        firstState = false;
        if (state.closedUs) el('closedUs').value = state.closedUs;
        if (state.openUs) el('openUs').value = state.openUs;
        el('servoDetails').open = !state.calibrated;
        el('carouselDetails').open = !state.carouselPositions.some(pulse => pulse > 0);
        selectCarouselCalibration();
      }
    } catch {
      token = ''; el('gate').textContent = 'Waiting for ESP32. Check both motors and the mechanism before another request.';
      invalidateRange('Unavailable — dashboard connection lost.');
      el('nfcStatus').textContent = 'Connection lost — tap-dispensing mode is unknown. Check the mechanism before scanning.';
      el('rangeState').textContent = 'No fresh status received from the ESP32.';
    }
  }
  controls(); setTimeout(refresh, 1000);
}
['aligned','positionConfirm','testConfirm','saveConfirm','carouselTestConfirm','carouselSaveConfirm'].forEach(id => el(id).addEventListener('change', controls));
el('fillConfirm').addEventListener('change', controls);
el('nfcConfirm').addEventListener('change', controls);
['empty','full'].forEach(kind => el(kind === 'empty' ? 'captureEmpty' : 'captureFull').addEventListener('click', () => {
  if(el(kind === 'empty' ? 'captureEmpty' : 'captureFull').disabled)return;
  el('fillConfirm').checked=false;
  post('/fill/capture',{kind,confirm:'1'},'fillMessage');
}));
el('clearFill').addEventListener('click', () => {
  if(el('clearFill').disabled || !confirm('Clear only the empty and full fill-height references? Neither motor will move.'))return;
  el('fillConfirm').checked=false;
  post('/fill/clear',{confirm:'1'},'fillMessage');
});
el('enableNfc').addEventListener('click', () => {
  if(el('enableNfc').disabled)return;
  clearMovementConfirmations();
  post('/nfc/mode',{enabled:'1',confirm:'1'},'nfcMessage');
});
el('pauseNfc').addEventListener('click', () => {
  if(el('pauseNfc').disabled)return;
  el('nfcConfirm').checked=false;
  post('/nfc/mode',{enabled:'0',confirm:'1'},'nfcMessage');
});
el('slot').addEventListener('change', () => {clearMovementConfirmations();controls();});
el('carouselSlot').addEventListener('change', selectCarouselCalibration);
['closedUs','openUs'].forEach(id => el(id).addEventListener('input', () => {el('saveConfirm').checked=false;el('testConfirm').checked=false;controls();}));
el('carouselUs').addEventListener('input', () => {el('carouselSaveConfirm').checked=false;el('carouselTestConfirm').checked=false;controls();});
el('positionButton').addEventListener('click', () => {
  if(el('positionButton').disabled)return;
  const slot=el('slot').value;
  if(!confirm('Close the SG90 flap, then position the SG92R carousel at compartment '+slot+'? Keep the mechanism clear.'))return;
  clearMovementConfirmations();
  post('/carousel/move',{slot,clear:'1'},'positionMessage');
});
el('releaseForm').addEventListener('submit', event => {
  event.preventDefault(); if(el('releaseButton').disabled)return;
  const slot=el('slot').value;
  if(!confirm('Open and close the SG90 flap for the visually checked compartment '+slot+'?'))return;
  clearMovementConfirmations();
  post('/release',{slot,aligned:'1'},'releaseMessage');
});
document.querySelectorAll('.testButton').forEach(button => button.addEventListener('click', () => {
  if(button.disabled)return;
  const role=button.dataset.role, value=el(role==='open'?'openUs':'closedUs').value;
  if(!confirm('Move the SG90 flap to '+value+' µs for 0.6 seconds? Keep the mechanism clear.'))return;
  clearMovementConfirmations();
  post('/servo/test',{role,pulse:value,clear:'1'},'servoMessage');
}));
el('saveServo').addEventListener('click', () => {
  if(el('saveServo').disabled)return;
  post('/servo/save',{open:el('openUs').value,closed:el('closedUs').value,verified:'1'},'servoMessage');
  el('saveConfirm').checked=false;
});
el('disableServo').addEventListener('click', () => {
  if(el('disableServo').disabled)return;
  clearMovementConfirmations();post('/servo/disable',{},'servoMessage');
});
el('testCarousel').addEventListener('click', () => {
  if(el('testCarousel').disabled)return;
  const slot=el('carouselSlot').value, pulse=el('carouselUs').value;
  if(!confirm('Move the SG92R carousel to '+pulse+' µs for about 1 second? This clears the saved position for compartment '+slot+'.'))return;
  clearMovementConfirmations();post('/carousel/test',{slot,pulse,clear:'1'},'carouselMessage');
});
el('saveCarousel').addEventListener('click', () => {
  if(el('saveCarousel').disabled)return;
  post('/carousel/save',{slot:el('carouselSlot').value,pulse:el('carouselUs').value,verified:'1'},'carouselMessage');
  el('carouselSaveConfirm').checked=false;
});
el('wifiForm').addEventListener('submit', event => {
  event.preventDefault(); if(el('joinWifi').disabled)return;
  post('/wifi',{ssid:el('ssid').value,password:el('wifiPassword').value},'wifiMessage');
  el('wifiPassword').value='';
});
el('useTag').addEventListener('click', () => {if(!el('useTag').disabled && state?.tag)el('uid').value=state.tag;});
el('tagForm').addEventListener('submit', event => {
  event.preventDefault();if(el('saveTag').disabled)return;
  post('/tag',{slot:el('tagSlot').value,uid:el('uid').value.toUpperCase().trim()},'tagMessage');
});
refresh();
</script></body></html>

)PVHTML";

// All methods are inside the class to avoid Arduino auto-prototype ordering issues.
class PharmaVault {
 private:
  static constexpr uint8_t GatePwm=3, CarouselPwm=5, Trigger=13, Echo=14, UartRx=4, UartTx=17;
  static constexpr uint8_t SlotCount=8, PwmBits=14, NoPin=255;
  static constexpr uint32_t TicketLifetimeMs=60000, CooldownMs=3000;
  static constexpr uint32_t GateMagic=0x50564732, CarouselMagic=0x50564335, NetworkMagic=0x50564E31, TagsMagic=0x50565431;
  static constexpr uint32_t ContentsMagic=0x50564531, RangeFreshMs=2000;
  static constexpr uint8_t RangeWindowSize=5, NfcCacheSize=32;
  static constexpr uint32_t FillMagic=0x50564631, ReaderFreshMs=16000;
  static constexpr uint16_t GateOpenMs=650, GateCloseMs=650, TestMs=600, CarouselMs=1000;
  enum class Phase:uint8_t {Idle,Queued,Opening,Closing,Testing,CarouselClosing,CarouselMoving,CarouselTesting};
  enum class Motion:uint8_t {Cycle,TestOpen,TestClosed,CarouselTest,CarouselMove,NfcCycle};
  enum class Result:uint8_t {None,Pending,PulsesSent,ServoFault};
  struct GateSettings {uint32_t magic;uint16_t openUs,closedUs;};
  struct CarouselSettings {uint32_t magic;uint16_t pulses[SlotCount];};
  struct NetworkSettings {uint32_t magic;char ssid[33];char password[65];};
  struct TagSettings {uint32_t magic;char uids[SlotCount][21];};
  // These references describe the fixed sensor's current view, not eight-compartment inventory.
  struct ContentsSettings {uint32_t magic;uint16_t emptyMm,presentMm;};
  // A separate key avoids treating an old arbitrary loaded sample as 100% full.
  struct FillSettings {uint32_t magic;uint16_t emptyMm,fullMm;};
  struct NfcReply {uint16_t seq;char command[8],argument[21],answer[40];};

  WebServer web{80};
  HardwareSerial msp{2};
  Preferences prefs;
  bool storageOk=false, servoFault=false, hadMotion=false, networkQueued=false;
  bool apStarted=false, wasConnected=false;
  GateSettings gate{};
  CarouselSettings carousel{};
  NetworkSettings network{};
  TagSettings tags{};
  ContentsSettings contents{};
  FillSettings fill{};
  bool nfcEnabled=false,readerSeen=false,nfcAwaitClear=true;
  uint32_t readerAt=0;
  char heldNfcUid[21]{};
  const char *nfcLastAction="No automatic cycle requested this session.";
  NfcReply nfcReplies[NfcCacheSize]{};
  uint8_t nfcReplyNext=0;
  char accessPassword[33]{}, apSsid[32]{}, token[33]{};
  uint32_t tokenAt=0, phaseAt=0, finishedAt=0;
  Phase phase=Phase::Idle;
  Motion motion=Motion::Cycle;
  Result lastResult[SlotCount]{};
  uint8_t gateSlot=0;
  uint8_t activePin=NoPin, carouselSlot=0, carouselTestSlot=0, carouselCommandedSlot=0;
  uint16_t carouselTestPulse=0;
  const char *carouselResult="No carousel position commanded this session.";
  uint16_t testPulse=0, testedOpen=0, testedClosed=0;
  const char *testResult="";
  char lastUid[21]{};
  int32_t temperature=-32768, humidity=65535;
  uint16_t environmentFlags=4, distanceMm=0;
  uint32_t environmentAt=0;
  bool environmentSeen=false;
  const char *rangeStatus="Waiting";
  uint32_t rawPulseUs=0,rangeRawMm=0,rangeAt=0,lastRangePoll=0;
  uint32_t rangeTriggers=0,rangeValidCount=0,rangeInvalidCount=0;
  bool rangeHasSample=false;
  uint16_t rangeWindow[RangeWindowSize]{},rangeMedianMm=0,rangeSpread=0;
  uint8_t rangeSamples=0,rangeNext=0;

  static bool elapsed(uint32_t now,uint32_t then,uint32_t ms) {return now-then>=ms;}
  static bool validPulse(uint16_t value) {return value>=900&&value<=2100;}
  static bool validRange(uint32_t mm) {return mm>=20&&mm<=3000;}
  bool emptyReferenceSet() const {
    return contents.magic==ContentsMagic&&validRange(contents.emptyMm);
  }
  bool contentsReferencesSet() const {
    return emptyReferenceSet()&&validRange(contents.presentMm)&&contents.presentMm+20U<=contents.emptyMm;
  }
  bool rangeFresh(uint32_t now) const {return rangeHasSample&&now-rangeAt<=RangeFreshMs;}
  bool rangeStable(uint32_t now) const {
    return phase==Phase::Idle&&!strcmp(rangeStatus,"Valid")&&rangeFresh(now)&&
      rangeSamples==RangeWindowSize&&rangeSpread<=10;
  }
  const char *contentsState(uint32_t now) const {
    if(phase!=Phase::Idle||strcmp(rangeStatus,"Valid")||!rangeFresh(now))return "UNKNOWN";
    if(!rangeStable(now))return "UNCERTAIN";
    if(!contentsReferencesSet())return "UNCALIBRATED";
    const uint16_t gap=contents.emptyMm-contents.presentMm;
    const uint16_t tolerance=gap/4U>5U?gap/4U:5U;
    const uint16_t lower=contents.emptyMm-tolerance,upper=contents.emptyMm+tolerance;
    if(rangeMedianMm>=lower&&rangeMedianMm<=upper)return "POSSIBLE_EMPTY";
    if(rangeMedianMm>=20&&rangeMedianMm<=contents.emptyMm-2U*tolerance)return "CONTENTS_PRESENT";
    return "UNCERTAIN";
  }
  const char *contentsText(uint32_t now) const {
    const char *state=contentsState(now);
    if(!strcmp(state,"POSSIBLE_EMPTY"))return "Possible empty in the current sensor view. Visually confirm and refill if needed.";
    if(!strcmp(state,"CONTENTS_PRESENT"))return "A nearer surface is detected in the current sensor view. This does not count pills or confirm intake.";
    if(!strcmp(state,"UNCALIBRATED"))return "Stable distance. Capture empty and contents-present references with the sensor fixed in place.";
    if(!strcmp(state,"UNCERTAIN"))return "Current sensor view is uncertain. Wait for five stable readings and check target alignment.";
    return "Contents unknown: no fresh valid echo or motor activity. A missing echo does not mean empty.";
  }
  bool emptyFillReferenceSet() const {
    return fill.magic==FillMagic&&validRange(fill.emptyMm);
  }
  bool fillReferencesSet() const {
    return emptyFillReferenceSet()&&validRange(fill.fullMm)&&fill.fullMm+20U<=fill.emptyMm;
  }
  int fillPercentage(uint32_t now) const {
    if(!rangeStable(now)||!fillReferencesSet())return -1;
    const int distance=int(rangeMedianMm),empty=int(fill.emptyMm),full=int(fill.fullMm);
    // A much farther wall or an unexpectedly close obstruction is not a useful fill reading.
    if(distance>empty+10||distance<full-10)return -1;
    if(distance>=empty)return 0;
    if(distance<=full)return 100;
    const int span=empty-full;
    return ((empty-distance)*100+span/2)/span;
  }
  const char *fillStatus(uint32_t now) const {
    if(phase!=Phase::Idle)return "Measuring paused during motor movement.";
    if(strcmp(rangeStatus,"Valid")||!rangeFresh(now))return "Unknown: no fresh valid echo. A missing echo is not 0% full.";
    if(!rangeStable(now))return "Waiting for five steady distance readings.";
    if(!fillReferencesSet())return "Capture empty (0%) and full (100%) references with the sensor fixed in place.";
    if(fillPercentage(now)<0)return "Outside the calibrated distance range. Check the viewed compartment and sensor position.";
    return "Estimated fill height in the current sensor view; not pill count or verified dispensing.";
  }
  bool readerFresh(uint32_t now) const {return readerSeen&&now-readerAt<=ReaderFreshMs;}
  bool hasMappedPosition() const {
    for(uint8_t i=0;i<SlotCount;++i)
      if(tags.uids[i][0]&&slotForUid(tags.uids[i])==i&&carouselSlotSet(i))return true;
    return false;
  }
  bool canEnableNfc(uint32_t now) const {return canRelease(now)&&readerFresh(now)&&hasMappedPosition();}
  const char *nfcStatus(uint32_t now) const {
    if(!readerFresh(now))return "Reader link unavailable. Run the updated MSP430 firmware and wait for its heartbeat.";
    if(!storageOk||servoFault)return "NFC dispensing unavailable: inspect the settings/PWM fault and restart.";
    if(!endpointsSet())return "Calibrate and save both SG90 flap positions before enabling NFC dispensing.";
    if(!hasMappedPosition())return "Assign a card to a compartment with a saved SG92R position.";
    if(!nfcEnabled)return "NFC dispensing paused. Card scans identify compartments and retrieve logs only.";
    if(phase!=Phase::Idle)return "Automatic/manual cycle in progress. Further taps are rejected, not queued.";
    if(!canTest(now))return "Wait for the three-second movement pause, then remove and retap the card.";
    if(nfcAwaitClear||heldNfcUid[0])return "Remove the card for at least one second; waiting for the reader to confirm clear before a new tap.";
    return "Ready: tap an assigned card to close, position, open and close the flap.";
  }
  void pauseNfc(const char *reason) {nfcEnabled=false;nfcAwaitClear=true;nfcLastAction=reason;}
  static bool validSecret(const char *value) {
    if(strlen(value)!=32)return false;
    for(size_t i=0;i<32;++i)if(pv_hex(value[i])<0)return false;
    return true;
  }
  static void randomHex(char *out) {
    for(size_t i=0;i<4;++i)snprintf(out+8*i,9,"%08lX",static_cast<unsigned long>(esp_random()));
  }
  bool endpointsSet() const {
    return gate.magic==GateMagic&&validPulse(gate.openUs)&&validPulse(gate.closedUs)&&gate.openUs!=gate.closedUs;
  }
  bool canTest(uint32_t now) const {
    return storageOk&&!servoFault&&phase==Phase::Idle&&(!hadMotion||elapsed(now,finishedAt,CooldownMs));
  }
  bool canRelease(uint32_t now) const {return endpointsSet()&&canTest(now);}
  bool canCarouselMove(uint32_t now) const {return canRelease(now);}
  bool carouselSlotSet(uint8_t slot) const {
    if(slot>=SlotCount||carousel.magic!=CarouselMagic||!validPulse(carousel.pulses[slot]))return false;
    for(uint8_t i=0;i<SlotCount;++i)if(i!=slot&&carousel.pulses[i]==carousel.pulses[slot])return false;
    return true;
  }
  const char *gateStatus(uint32_t now) const {
    if(!storageOk)return "Settings storage unavailable. Restart before moving the servo.";
    if(servoFault)return "PWM fault. Inspect the flap before restarting the ESP32.";
    if(phase!=Phase::Idle){
      if(motion==Motion::NfcCycle)return "NFC cycle: close flap, position carousel, then open and close flap.";
      if(motion==Motion::CarouselMove)return "Closing flap, then commanding carousel position.";
      if(motion==Motion::CarouselTest)return "Carousel position test in progress.";
      return motion==Motion::Cycle?"Flap cycle in progress.":"Flap position test in progress.";
    }
    if(!endpointsSet())return "Set and save the servo positions below to enable flap cycles.";
    if(!canRelease(now))return "Pause between servo movements.";
    return "Ready. Visually verify compartment alignment and confirm below.";
  }
  static const char *resultCode(Result result) {
    switch(result){case Result::Pending:return "DEMO_PENDING";case Result::PulsesSent:return "DEMO_PULSE_SENT";
      case Result::ServoFault:return "DEMO_SERVO_FAULT";default:return "NO_LOG";}
  }
  static const char *resultText(Result result) {
    switch(result){case Result::Pending:return "Flap cycle requested";
      case Result::PulsesSent:return "Open and close PWM sent. Inspect the flap and contents.";
      case Result::ServoFault:return "PWM error. Inspect the flap.";default:return "No request";}
  }
  bool saveBytes(const char *key,const void *value,size_t length) {
    if(!storageOk)return false;
    if(prefs.putBytes(key,value,length)==length)return true;
    storageOk=false;pauseNfc("NFC dispensing paused after a settings storage failure.");
    return false;
  }
  bool disarm() {
    GateSettings disabled{};
    if(!saveBytes("flap3",&disabled,sizeof(disabled)))return false;
    gate=disabled;
    return true;
  }
  void loadSettings() {
    storageOk=prefs.begin("pharmavault",false);
    String saved=storageOk?prefs.getString("access",""):String("");
    if(saved.length()==32&&validSecret(saved.c_str())){
      memcpy(accessPassword,saved.c_str(),33);
    }else{
      randomHex(accessPassword);
      if(storageOk&&prefs.putString("access",accessPassword)!=32)storageOk=false;
    }
    if(!storageOk)return;
    // Old gate settings drove GPIO5. Never apply them to the SG90 on GPIO3.
    if(prefs.getBytesLength("flap3")==sizeof(gate))prefs.getBytes("flap3",&gate,sizeof(gate));
    if(!endpointsSet())gate=GateSettings{};
    if(prefs.getBytesLength("carousel5")==sizeof(carousel))prefs.getBytes("carousel5",&carousel,sizeof(carousel));
    if(carousel.magic!=CarouselMagic)carousel=CarouselSettings{};
    for(uint8_t i=0;i<SlotCount;++i)if(!validPulse(carousel.pulses[i]))carousel.pulses[i]=0;
    for(uint8_t i=0;i<SlotCount;++i){
      for(uint8_t j=i+1;j<SlotCount;++j){
        if(carousel.pulses[i]&&carousel.pulses[i]==carousel.pulses[j]){
          carousel=CarouselSettings{};break; // Ambiguous stored mapping must be recalibrated.
        }
      }
    }
    if(prefs.getBytesLength("network")==sizeof(network))prefs.getBytes("network",&network,sizeof(network));
    if(network.magic!=NetworkMagic||!memchr(network.ssid,'\0',sizeof(network.ssid))||
        !memchr(network.password,'\0',sizeof(network.password))||!network.ssid[0]||
        strlen(network.password)>63||(network.password[0]&&strlen(network.password)<8))network=NetworkSettings{};
    if(prefs.getBytesLength("tags")==sizeof(tags))prefs.getBytes("tags",&tags,sizeof(tags));
    if(tags.magic!=TagsMagic)tags=TagSettings{};
    for(uint8_t i=0;i<SlotCount;++i){
      if(!memchr(tags.uids[i],'\0',sizeof(tags.uids[i]))||
          (tags.uids[i][0]&&!pv_uid_valid(tags.uids[i])))tags.uids[i][0]='\0';
    }
    // Duplicate saved mappings are ambiguous and remain unrecognized by slotForUid.
    if(prefs.getBytesLength("contents1")==sizeof(contents))prefs.getBytes("contents1",&contents,sizeof(contents));
    if(!emptyReferenceSet()||(contents.presentMm&&!contentsReferencesSet()))contents=ContentsSettings{};
    if(prefs.getBytesLength("fill1")==sizeof(fill))prefs.getBytes("fill1",&fill,sizeof(fill));
    if(!emptyFillReferenceSet()||(fill.fullMm&&!fillReferencesSet()))fill=FillSettings{};
  }
  bool uartSend(const char *body) {
    char frame[PV_FRAME_SIZE];const size_t length=pv_encode(body,frame,sizeof(frame));
    return length&&msp.availableForWrite()>=static_cast<int>(length)&&
      msp.write(reinterpret_cast<const uint8_t*>(frame),length)==length;
  }
  void sendLcd(const char *state,int slot) {
    char body[64];snprintf(body,sizeof(body),"PV1,0,UI,%s,%d",state,slot);uartSend(body);
  }
  bool writePulse(uint16_t pulseUs) {
    if(activePin==NoPin||!validPulse(pulseUs))return false;
    const uint32_t duty=(uint32_t(pulseUs)*(1UL<<PwmBits)+10000UL)/20000UL;
    return ledcWrite(activePin,duty);
  }
  bool startPwm(uint8_t pin,uint16_t pulseUs) {
    if(activePin!=NoPin||!validPulse(pulseUs))return false;
    markRangeMoving();
    if(!ledcAttach(pin,50,PwmBits))return false;
    activePin=pin;
    return writePulse(pulseUs);
  }
  bool stopPwm() {
    if(activePin==NoPin)return true;
    const uint8_t pin=activePin;activePin=NoPin;
    const bool ok=ledcDetach(pin);
    pinMode(pin,OUTPUT);digitalWrite(pin,LOW);
    return ok;
  }
  void finishMotion(bool ok) {
    if(!stopPwm())ok=false;
    phase=Phase::Idle;finishedAt=millis();hadMotion=true;servoFault=!ok;
    if(!ok){carouselCommandedSlot=0;pauseNfc("NFC dispensing paused after a PWM fault. Inspect the mechanism and restart.");}
    if(motion==Motion::NfcCycle){
      lastResult[gateSlot]=ok?Result::PulsesSent:Result::ServoFault;
      carouselCommandedSlot=ok?carouselSlot+1:0;
      nfcLastAction=ok?"NFC cycle PWM completed: carousel position and flap open/close commanded. Inspect the actual contents.":
        "NFC cycle failed. Inspect both motors and the flap before restarting.";
      carouselResult=nfcLastAction;
      sendLcd(resultCode(lastResult[gateSlot]),gateSlot);
    }else if(motion==Motion::Cycle){
      lastResult[gateSlot]=ok?Result::PulsesSent:Result::ServoFault;
      sendLcd(resultCode(lastResult[gateSlot]),gateSlot);
    }else if(motion==Motion::CarouselTest){
      carouselCommandedSlot=0; // A raw test is not a saved compartment position.
      if(ok){
        carouselTestSlot=carouselSlot+1;carouselTestPulse=testPulse;
        carouselResult="Carousel test PWM completed. Verify alignment before saving this position.";
      }else{
        carouselTestSlot=0;carouselTestPulse=0;
        carouselResult="Carousel PWM failed. Inspect and restart.";
      }
      sendLcd(ok?"CAROUSEL_TESTED":"DEMO_SERVO_FAULT",ok?carouselSlot:-1);
    }else if(motion==Motion::CarouselMove){
      carouselCommandedSlot=ok?carouselSlot+1:0;
      carouselResult=ok?"Carousel position commanded. Visually verify alignment before a flap cycle.":
        "Carousel movement PWM failed. Inspect and restart.";
      sendLcd(ok?"CAROUSEL_SENT":"DEMO_SERVO_FAULT",ok?carouselSlot:-1);
    }else{
      if(ok){
        if(motion==Motion::TestOpen)testedOpen=testPulse;else testedClosed=testPulse;
        testResult="Position PWM completed. Visually check the mechanism.";
      }else{
        testedOpen=testedClosed=0;testResult="PWM failed. Inspect and restart.";
      }
      sendLcd(ok?"CALIBRATE":"DEMO_SERVO_FAULT",-1);
    }
  }
  void runMotion() {
    if(phase!=Phase::Idle)markRangeMoving();
    const uint32_t now=millis();
    switch(phase){
      case Phase::Queued:{
        if(motion==Motion::CarouselTest){
          sendLcd("CAROUSEL_TEST",carouselSlot);
          if(!startPwm(CarouselPwm,testPulse)){finishMotion(false);break;}
          phase=Phase::CarouselTesting;
        }else if(motion==Motion::CarouselMove||motion==Motion::NfcCycle){
          sendLcd(motion==Motion::NfcCycle?"NFC_CLOSING":"CAROUSEL_MOVING",carouselSlot);
          // Close and stop the flap servo before energizing the carousel servo.
          if(!startPwm(GatePwm,gate.closedUs)){finishMotion(false);break;}
          phase=Phase::CarouselClosing;
        }else{
          sendLcd(motion==Motion::Cycle?"DEMO_MOVING":"CALIBRATE",motion==Motion::Cycle?gateSlot:-1);
          const uint16_t pulse=motion==Motion::Cycle?gate.openUs:testPulse;
          if(!startPwm(GatePwm,pulse)){finishMotion(false);break;}
          phase=motion==Motion::Cycle?Phase::Opening:Phase::Testing;
        }
        phaseAt=millis();break;
      }
      case Phase::Opening:
        if(elapsed(now,phaseAt,GateOpenMs)){
          if(!writePulse(gate.closedUs)){finishMotion(false);break;}
          if(motion==Motion::NfcCycle)sendLcd("NFC_CLOSING_END",gateSlot);
          phase=Phase::Closing;phaseAt=millis();
        }break;
      case Phase::Closing:if(elapsed(now,phaseAt,GateCloseMs))finishMotion(true);break;
      case Phase::Testing:if(elapsed(now,phaseAt,TestMs))finishMotion(true);break;
      case Phase::CarouselClosing:
        if(elapsed(now,phaseAt,GateCloseMs)){
          if(!stopPwm()){finishMotion(false);break;}
          if(!startPwm(CarouselPwm,carousel.pulses[carouselSlot])){finishMotion(false);break;}
          if(motion==Motion::NfcCycle)sendLcd("NFC_POSITION",carouselSlot);
          phase=Phase::CarouselMoving;phaseAt=millis();
        }break;
      case Phase::CarouselMoving:
        if(elapsed(now,phaseAt,CarouselMs)){
          if(motion!=Motion::NfcCycle){finishMotion(true);break;}
          if(!stopPwm()){finishMotion(false);break;}
          carouselCommandedSlot=carouselSlot+1;
          if(!startPwm(GatePwm,gate.openUs)){finishMotion(false);break;}
          sendLcd("NFC_OPENING",gateSlot);
          phase=Phase::Opening;phaseAt=millis();
        }
        break;
      case Phase::CarouselTesting:
        if(elapsed(now,phaseAt,CarouselMs))finishMotion(true);
        break;
      case Phase::Idle:break;
    }
  }
  void resetRangeWindow(const char *status) {
    rangeStatus=status;distanceMm=0;rangeSamples=0;rangeNext=0;rangeMedianMm=0;rangeSpread=0;
  }
  void markRangeMoving() {
    resetRangeWindow("Moving");rawPulseUs=0;rangeRawMm=0;
  }
  void addRangeSample(uint16_t mm) {
    rangeWindow[rangeNext]=mm;rangeNext=(rangeNext+1)%RangeWindowSize;
    if(rangeSamples<RangeWindowSize)++rangeSamples;
    uint16_t sorted[RangeWindowSize];
    for(uint8_t i=0;i<rangeSamples;++i)sorted[i]=rangeWindow[i];
    for(uint8_t i=1;i<rangeSamples;++i){
      const uint16_t value=sorted[i];uint8_t j=i;
      while(j&&sorted[j-1]>value){sorted[j]=sorted[j-1];--j;}sorted[j]=value;
    }
    rangeSpread=sorted[rangeSamples-1]-sorted[0];
    rangeMedianMm=rangeSamples==RangeWindowSize?sorted[RangeWindowSize/2]:0;
  }
  void pollDistance(uint32_t now) {
    if(phase!=Phase::Idle){markRangeMoving();return;}
    if(!elapsed(now,lastRangePoll,500))return;
    if(rangeHasSample&&!rangeFresh(now))resetRangeWindow("Waiting");
    lastRangePoll=now;rangeAt=now;rangeHasSample=true;distanceMm=0;rawPulseUs=0;rangeRawMm=0;
    // A high ECHO before TRIG is not an empty compartment: do not retrigger a busy/faulted module.
    if(digitalRead(Echo)==HIGH){resetRangeWindow("IdleHigh");++rangeInvalidCount;return;}
    digitalWrite(Trigger,HIGH);delayMicroseconds(12);digitalWrite(Trigger,LOW);++rangeTriggers;
    const unsigned long duration=pulseIn(Echo,HIGH,25000);
    rawPulseUs=duration;
    const unsigned long mm=(duration*343UL+1000UL)/2000UL;
    rangeRawMm=mm;
    if(!duration){resetRangeWindow("NoPulse");++rangeInvalidCount;return;}
    if(!validRange(mm)){resetRangeWindow("OutOfRange");++rangeInvalidCount;return;}
    rangeStatus="Valid";distanceMm=static_cast<uint16_t>(mm);++rangeValidCount;
    addRangeSample(distanceMm);
  }
  int slotForUid(const char *uid) const {
    int found=-1;if(!uid[0])return -1;
    for(uint8_t i=0;i<SlotCount;++i){if(strcmp(tags.uids[i],uid))continue;if(found>=0)return -1;found=i;}
    return found;
  }
  const char *receiveEnvironment(char **fields) {
    int32_t temp,rh;uint16_t flags;
    if(!pv_i32(fields[3],&temp)||!pv_i32(fields[4],&rh)||!pv_u16(fields[5],&flags)||flags>7)return "ACK,ERR,BAD_ENV";
    const bool tv=temp>=-4000&&temp<=12500, hv=rh>=0&&rh<=10000;
    if((!tv&&temp!=-32768)||(!hv&&rh!=65535)||((!tv||!hv)&&!(flags&4)))return "ACK,ERR,BAD_ENV";
    temperature=temp;humidity=rh;environmentFlags=flags;environmentAt=millis();environmentSeen=true;
    return "ACK,OK,ENV";
  }
  const char *cachedNfcReply(uint16_t seq,const char *command,const char *argument) const {
    for(uint8_t i=0;i<NfcCacheSize;++i){
      const NfcReply &entry=nfcReplies[i];
      if(entry.seq==seq&&!strcmp(entry.command,command)&&!strcmp(entry.argument,argument))return entry.answer;
    }
    return nullptr;
  }
  void cacheNfcReply(uint16_t seq,const char *command,const char *argument,const char *answer) {
    NfcReply &entry=nfcReplies[nfcReplyNext];nfcReplyNext=(nfcReplyNext+1)%NfcCacheSize;
    entry.seq=seq;snprintf(entry.command,sizeof(entry.command),"%s",command);
    snprintf(entry.argument,sizeof(entry.argument),"%s",argument);
    snprintf(entry.answer,sizeof(entry.answer),"%s",answer);
  }
  const char *receiveTag(const char *uid) {
    memcpy(lastUid,uid,strlen(uid)+1);
    const int slot=slotForUid(uid);
    const bool held=heldNfcUid[0];
    if(!held)memcpy(heldNfcUid,uid,strlen(uid)+1);
    // A different card without a matching CLEAR is also not a new presentation.
    if(slot<0){nfcLastAction="Unassigned card: no movement.";return "ACK,ERR,TAG_UNKNOWN";}
    if(!nfcEnabled){nfcLastAction="Card recognized while NFC dispensing is paused: no movement.";return "ACK,OK,TAG_KNOWN";}
    if(held||nfcAwaitClear){nfcLastAction="Remove the card for at least one second; wait for reader-clear confirmation, then retap.";return "ACK,ERR,REMOVE_CARD";}
    if(!readerFresh(millis())){pauseNfc("Reader heartbeat lost. Re-enable NFC dispensing after the link recovers.");return "ACK,ERR,NFC_NOT_READY";}
    if(phase!=Phase::Idle){nfcLastAction="Tap rejected while a motor cycle was busy; no action queued.";return "ACK,ERR,NFC_BUSY";}
    if(!storageOk||servoFault||!endpointsSet()){nfcLastAction="Tap rejected: settings, PWM or flap calibration unavailable.";return "ACK,ERR,NFC_NOT_READY";}
    if(!carouselSlotSet(static_cast<uint8_t>(slot))){nfcLastAction="Tap rejected: this compartment has no saved carousel position.";return "ACK,ERR,SLOT_UNSET";}
    if(!canTest(millis())){nfcLastAction="Tap rejected during the movement pause. Wait, remove and retap.";return "ACK,ERR,NFC_COOLDOWN";}
    gateSlot=static_cast<uint8_t>(slot);carouselSlot=gateSlot;carouselCommandedSlot=0;
    lastResult[gateSlot]=Result::Pending;motion=Motion::NfcCycle;phase=Phase::Queued;
    token[0]='\0';markRangeMoving();
    nfcLastAction="NFC cycle accepted: close flap, position assigned compartment, then open and close flap.";
    carouselResult=nfcLastAction;
    return "ACK,OK,DISPENSE_QUEUED";
  }
  void handleUart(char *line) {
    char *fields[8];const size_t count=pv_split(line,fields,8);uint16_t seq;
    if(count<3||!pv_u16(fields[1],&seq)||!seq)return;
    const char *answer="ACK,ERR,BAD_COMMAND";char log[64];
    const bool readerCommand=count==4&&!strcmp(fields[2],"READER")&&
      (!strcmp(fields[3],"BOOT")||!strcmp(fields[3],"ONLINE")||!strcmp(fields[3],"EMPTY"));
    const bool uidCommand=count==4&&pv_uid_valid(fields[3])&&
      (!strcmp(fields[2],"TAG")||!strcmp(fields[2],"CLEAR"));
    const bool cachedCommand=readerCommand||uidCommand;
    // A BOOT retry can only pause operation; even a sequence collision must not hide a reboot.
    if(readerCommand&&!strcmp(fields[3],"BOOT")){
      pauseNfc("MSP430 restarted. Check the mechanism, then enable NFC dispensing again.");
      heldNfcUid[0]='\0';readerSeen=false;
      // The MSP430 sequence seed can repeat after reset. BOOT starts a new replay-cache epoch.
      // A BOOT retry also pauses/unreadies NFC, so clearing here cannot authorize another cycle.
      memset(nfcReplies,0,sizeof(nfcReplies));nfcReplyNext=0;
    }
    const char *cached=cachedCommand?cachedNfcReply(seq,fields[2],fields[3]):nullptr;
    if(cached)answer=cached;
    else if(count==6&&!strcmp(fields[2],"ENV"))answer=receiveEnvironment(fields);
    else if(readerCommand){
      // BOOT can report a disconnected/faulted reader; only a healthy heartbeat establishes readiness.
      if(strcmp(fields[3],"BOOT")){readerSeen=true;readerAt=millis();}
      if(!strcmp(fields[3],"EMPTY")){
        heldNfcUid[0]='\0';nfcAwaitClear=false;answer="ACK,OK,READER_EMPTY";
      }else answer=!strcmp(fields[3],"BOOT")?"ACK,OK,READER_BOOT":"ACK,OK,READER_ONLINE";
    }else if(uidCommand){
      if(!strcmp(fields[2],"TAG"))answer=receiveTag(fields[3]);
      else if(!strcmp(heldNfcUid,fields[3])){heldNfcUid[0]='\0';nfcAwaitClear=false;answer="ACK,OK,CARD_REMOVED";}
      else if(!heldNfcUid[0]){nfcAwaitClear=false;answer="ACK,OK,NO_CARD";}
      else answer="ACK,OK,NO_CARD";
    }else if(count==4&&!strcmp(fields[2],"LOGREQ")&&pv_uid_valid(fields[3])){
      const int slot=slotForUid(fields[3]);
      if(slot<0)answer="ACK,ERR,TAG_UNKNOWN";
      else{snprintf(log,sizeof(log),"LOG,%d,%s,0",slot,resultCode(lastResult[slot]));answer=log;}
    }
    // Commit the decision before returning to the loop: retries cannot initiate another action.
    if(cachedCommand&&!cached)cacheNfcReply(seq,fields[2],fields[3],answer);
    char body[PV_FRAME_SIZE];snprintf(body,sizeof(body),"PV1,%u,%s",static_cast<unsigned>(seq),answer);uartSend(body);
  }
  void pollUart() {
    static char line[PV_FRAME_SIZE];static size_t used=0;static bool overflow=false;
    for(unsigned n=0;n<128&&msp.available();++n){
      const int c=msp.read();if(c=='\r')continue;
      if(c=='\n'){
        if(used&&!overflow){line[used]='\0';handleUart(line);}used=0;overflow=false;
      }else if(!overflow&&used<sizeof(line)-1&&c>=32&&c<=126)line[used++]=static_cast<char>(c);
      else overflow=true;
    }
  }
  void noCache(){web.sendHeader("Cache-Control","no-store");}
  bool authorize() {
    if(web.authenticate("demo",accessPassword))return true;
    web.requestAuthentication(BASIC_AUTH,"PharmaVault");return false;
  }
  const char *activeToken(uint32_t now) {
    if(phase!=Phase::Idle)return "";
    if(!token[0]||elapsed(now,tokenAt,TicketLifetimeMs)){randomHex(token);tokenAt=now;}
    return token;
  }
  bool consumeToken() {
    if(phase!=Phase::Idle||!token[0]||elapsed(millis(),tokenAt,TicketLifetimeMs)||web.arg("token")!=token){
      web.send(409,"text/plain","Request expired or already used. Refresh status and check the mechanism before retrying.");return false;
    }
    token[0]='\0';return true;
  }
  bool requireStorage() {
    if(storageOk)return true;
    web.send(503,"text/plain","Settings could not be saved. Restart the ESP32 and inspect Serial Monitor.");return false;
  }
  void sendState() {
    if(!authorize())return;
    const uint32_t now=millis();const bool fresh=environmentSeen&&!elapsed(now,environmentAt,16000);
    const char *environment=!fresh?"Waiting for MSP430 readings":(environmentFlags&4)?"Sensor unavailable or fault reported":
      environmentFlags?"Outside configured demo storage limits":"Within configured demo storage limits";
    char temp[12]="null",rh[12]="null",mm[12]="null";
    if(fresh&&temperature!=-32768)snprintf(temp,sizeof(temp),"%ld",static_cast<long>(temperature));
    if(fresh&&humidity!=65535)snprintf(rh,sizeof(rh),"%ld",static_cast<long>(humidity));
    if(distanceMm&&phase==Phase::Idle&&rangeFresh(now))snprintf(mm,sizeof(mm),"%u",static_cast<unsigned>(distanceMm));
    const String lanIp=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");
    char header[1400];
    snprintf(header,sizeof(header),
      "{\"gate\":\"%s\",\"token\":\"%s\",\"canRelease\":%s,\"canTest\":%s,\"storageOk\":%s,"
      "\"calibrated\":%s,\"openUs\":%u,\"closedUs\":%u,\"testOpen\":%u,\"testClosed\":%u,\"testResult\":\"%s\","
      "\"temp\":%s,\"rh\":%s,\"mm\":%s,\"environment\":\"%s\",\"tag\":\"%s\",\"tagSlot\":%d,\"lanIp\":\"%s\",\"log\":[",
      gateStatus(now),activeToken(now),canRelease(now)?"true":"false",canTest(now)?"true":"false",storageOk?"true":"false",
      endpointsSet()?"true":"false",unsigned(gate.openUs),unsigned(gate.closedUs),unsigned(testedOpen),unsigned(testedClosed),testResult,
      temp,rh,mm,environment,lastUid,lastUid[0]?slotForUid(lastUid)+1:0,lanIp.c_str());
    String json(header);json.reserve(3600);
    for(uint8_t i=0;i<SlotCount;++i){if(i)json+=',';json+='"';json+=resultText(lastResult[i]);json+='"';}
    json+="],\"mappings\":[";
    for(uint8_t i=0;i<SlotCount;++i){if(i)json+=',';json+='"';json+=tags.uids[i];json+='"';}
    json+="],\"carouselPositions\":[";
    for(uint8_t i=0;i<SlotCount;++i){if(i)json+=',';json+=String(unsigned(carousel.pulses[i]));}
    char carouselState[420];
    snprintf(carouselState,sizeof(carouselState),
      "],\"carouselTestSlot\":%u,\"carouselTestPulse\":%u,\"carouselResult\":\"%s\","
      "\"carouselCommandedSlot\":%u,\"canCarouselMove\":%s,",
      unsigned(carouselTestSlot),unsigned(carouselTestPulse),carouselResult,
      unsigned(carouselCommandedSlot),canCarouselMove(now)?"true":"false");
    json+=carouselState;
    char rangeAge[16]="null",rangeState[1000];
    if(rangeHasSample)snprintf(rangeAge,sizeof(rangeAge),"%lu",static_cast<unsigned long>(now-rangeAt));
    snprintf(rangeState,sizeof(rangeState),
      "\"rangeStatus\":\"%s\",\"echoUs\":%lu,\"rangeRawMm\":%lu,\"rangeStable\":%s,"
      "\"rangeSamples\":%u,\"rangeSpread\":%u,\"rangeAgeMs\":%s,\"rangeMedianMm\":%u,"
      "\"contentsState\":\"%s\",\"contentsText\":\"%s\",\"emptyRefMm\":%u,\"presentRefMm\":%u,\"canCaptureRange\":%s",
      phase==Phase::Idle?rangeStatus:"Moving",static_cast<unsigned long>(rawPulseUs),static_cast<unsigned long>(rangeRawMm),
      rangeStable(now)?"true":"false",unsigned(rangeSamples),unsigned(rangeSpread),rangeAge,unsigned(rangeMedianMm),
      contentsState(now),contentsText(now),unsigned(contents.emptyMm),unsigned(contents.presentMm),
      storageOk&&rangeStable(now)?"true":"false");
    json+=rangeState;
    char fillValue[8]="null",newState[1200];const int percentage=fillPercentage(now);
    if(percentage>=0)snprintf(fillValue,sizeof(fillValue),"%d",percentage);
    snprintf(newState,sizeof(newState),
      ",\"fillPercent\":%s,\"fillStatus\":\"%s\",\"fillEmptyMm\":%u,\"fillFullMm\":%u,\"canCaptureFill\":%s,"
      "\"nfcEnabled\":%s,\"nfcStatus\":\"%s\",\"nfcLastAction\":\"%s\",\"nfcReady\":%s,\"canEnableNfc\":%s,\"motionActive\":%s}",
      fillValue,fillStatus(now),unsigned(fill.emptyMm),unsigned(fill.fullMm),
      storageOk&&!nfcEnabled&&rangeStable(now)?"true":"false",nfcEnabled?"true":"false",nfcStatus(now),nfcLastAction,
      readerFresh(now)?"true":"false",canEnableNfc(now)?"true":"false",phase!=Phase::Idle?"true":"false");
    json+=newState;noCache();web.send(200,"application/json",json);
  }
  void setNfcMode() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    const String enabled=web.arg("enabled");
    if((enabled!="1"&&enabled!="0")||web.arg("confirm")!="1"){
      web.send(400,"text/plain","Choose enabled 1 or 0 and confirm the NFC operating mode.");return;
    }
    if(enabled=="1"&&!canEnableNfc(millis())){web.send(409,"text/plain",nfcStatus(millis()));return;}
    if(!consumeToken())return;
    nfcEnabled=enabled=="1";nfcAwaitClear=true;
    nfcLastAction=nfcEnabled?"NFC dispensing enabled. Remove any card already present, then tap for one cycle.":
      "NFC dispensing paused. Card identification and log lookup remain available.";
    web.send(200,"text/plain",nfcLastAction);
  }
  void captureFill() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    const String kind=web.arg("kind");
    if((kind!="empty"&&kind!="full")||web.arg("confirm")!="1"){
      web.send(400,"text/plain","Choose empty or full and confirm the fixed sensor and reference level.");return;
    }
    if(nfcEnabled||!rangeStable(millis())){
      web.send(409,"text/plain","Pause NFC dispensing. Keep the sensor fixed and wait for five fresh steady readings with idle motors.");return;
    }
    if(kind=="full"&&(!emptyFillReferenceSet()||rangeMedianMm+20U>fill.emptyMm)){
      web.send(400,"text/plain","Capture empty first. The full surface must be at least 20 mm nearer, and at least 20 mm from the sensor.");return;
    }
    if(!consumeToken())return;
    FillSettings updated=fill;updated.magic=FillMagic;
    if(kind=="empty"){updated.emptyMm=rangeMedianMm;updated.fullMm=0;}else updated.fullMm=rangeMedianMm;
    if(!saveBytes("fill1",&updated,sizeof(updated))){requireStorage();return;}
    fill=updated;
    web.send(200,"text/plain",kind=="empty"?"Empty (0%) saved. Previous full reference cleared; now capture the intended full level.":
      "Full (100%) saved. Percentage estimates contents height in the current sensor view.");
  }
  void clearFill() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    if(nfcEnabled){web.send(409,"text/plain","Pause NFC dispensing before clearing fill calibration.");return;}
    if(web.arg("confirm")!="1"){web.send(400,"text/plain","Confirm clearing the empty and full references.");return;}
    if(!consumeToken())return;
    const FillSettings cleared{};
    if(!saveBytes("fill1",&cleared,sizeof(cleared))){requireStorage();return;}
    fill=cleared;web.send(200,"text/plain","Fill references cleared. Other saved settings are unchanged.");
  }
  void captureContents() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    const String kind=web.arg("kind");
    if((kind!="empty"&&kind!="present")||web.arg("confirm")!="1"){
      web.send(400,"text/plain","Choose empty or present and confirm the fixed sensor and target arrangement.");return;
    }
    if(!rangeStable(millis())){
      web.send(409,"text/plain","Keep the sensor and target still. Capture needs five valid readings within 10 mm, a fresh echo, and idle motors.");return;
    }
    if(kind=="present"&&(!emptyReferenceSet()||rangeMedianMm+20U>contents.emptyMm)){
      web.send(400,"text/plain","Capture the empty reference first. The contents-present surface must be at least 20 mm nearer than the empty reference.");return;
    }
    if(!consumeToken())return;
    ContentsSettings updated=contents;updated.magic=ContentsMagic;
    if(kind=="empty"){updated.emptyMm=rangeMedianMm;updated.presentMm=0;}
    else updated.presentMm=rangeMedianMm;
    if(!saveBytes("contents1",&updated,sizeof(updated))){requireStorage();return;}
    contents=updated;
    web.send(200,"text/plain",kind=="empty"?
      "Empty reference saved; previous contents-present reference cleared. Add a detectable target and capture present without moving the sensor.":
      "Contents-present reference saved. Classification applies only to the current sensor view; visually verify empty indications.");
  }
  void clearContents() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    if(web.arg("confirm")!="1"){
      web.send(400,"text/plain","Confirm clearing the two ultrasonic references.");return;
    }
    if(!consumeToken())return;
    const ContentsSettings cleared{};
    if(!saveBytes("contents1",&cleared,sizeof(cleared))){requireStorage();return;}
    contents=cleared;
    web.send(200,"text/plain","Ultrasonic references cleared. Servo positions, tag assignments and network settings are unchanged.");
  }
  void requestCycle() {
    if(!authorize())return;noCache();
    if(!canRelease(millis())){web.send(409,"text/plain",gateStatus(millis()));return;}
    uint16_t selected;
    if(!pv_u16(web.arg("slot").c_str(),&selected)||selected<1||selected>SlotCount||web.arg("aligned")!="1"){
      web.send(400,"text/plain","Select a compartment and confirm visual alignment.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused for manual flap operation.");
    gateSlot=static_cast<uint8_t>(selected-1);lastResult[gateSlot]=Result::Pending;
    motion=Motion::Cycle;phase=Phase::Queued;
    markRangeMoving();
    web.send(202,"text/plain","Flap cycle requested. Observe closure and check the command log.");
  }
  void requestTest() {
    if(!authorize())return;noCache();
    if(!canTest(millis())){web.send(409,"text/plain","Wait between movements. Restart after a PWM or storage fault.");return;}
    uint16_t pulse;const String role=web.arg("role");
    if(!pv_u16(web.arg("pulse").c_str(),&pulse)||!validPulse(pulse)||(role!="open"&&role!="closed")||web.arg("clear")!="1"){
      web.send(400,"text/plain","Choose open or closed, 900 to 2100 microseconds, and confirm the mechanism is clear.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused for flap calibration.");
    // Persist disarm BEFORE allowing another calibration movement. A reset cannot re-arm stale endpoints.
    if(!disarm()){requireStorage();return;}
    testPulse=pulse;motion=role=="open"?Motion::TestOpen:Motion::TestClosed;phase=Phase::Queued;
    markRangeMoving();
    if(motion==Motion::TestOpen)testedOpen=0;else testedClosed=0;
    testResult="Position test requested.";
    web.send(202,"text/plain","Position test requested for 0.6 seconds. Check its travel before saving.");
  }
  void saveServo() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    uint16_t opened=0,closed=0;
    if(servoFault||phase!=Phase::Idle||!pv_u16(web.arg("open").c_str(),&opened)||
        !pv_u16(web.arg("closed").c_str(),&closed)||!validPulse(opened)||!validPulse(closed)||opened==closed||
        opened!=testedOpen||closed!=testedClosed||web.arg("verified")!="1"){
      web.send(400,"text/plain","Successfully test both different positions this session, then confirm the linkage checks.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused after flap settings changed.");
    GateSettings updated{GateMagic,opened,closed};
    if(!saveBytes("flap3",&updated,sizeof(updated))){requireStorage();return;}
    gate=updated;
    web.send(200,"text/plain","Positions saved. Flap cycles are enabled after the movement pause.");
  }
  void disableServo() {
    if(!authorize())return;noCache();if(!requireStorage()||!consumeToken())return;
    pauseNfc("NFC dispensing paused: flap disabled.");
    if(!disarm()){requireStorage();return;}testedOpen=testedClosed=0;
    web.send(200,"text/plain","Flap disabled. Readings and card scans continue.");
  }
  void requestCarouselTest() {
    if(!authorize())return;noCache();
    if(!canTest(millis())){web.send(409,"text/plain","Wait between movements. Restart after a PWM or storage fault.");return;}
    uint16_t selected=0,pulse=0;
    if(!pv_u16(web.arg("slot").c_str(),&selected)||selected<1||selected>SlotCount||
        !pv_u16(web.arg("pulse").c_str(),&pulse)||!validPulse(pulse)||web.arg("clear")!="1"){
      web.send(400,"text/plain","Choose compartment 1 to 8, 900 to 2100 microseconds, and confirm the carousel is clear and unloaded.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused for carousel calibration.");
    // Invalidate the selected mapping persistently BEFORE its calibration test.
    CarouselSettings updated=carousel;updated.magic=CarouselMagic;updated.pulses[selected-1]=0;
    if(!saveBytes("carousel5",&updated,sizeof(updated))){requireStorage();return;}
    carousel=updated;carouselSlot=static_cast<uint8_t>(selected-1);
    carouselTestSlot=0;carouselTestPulse=0;carouselCommandedSlot=0;
    testPulse=pulse;motion=Motion::CarouselTest;phase=Phase::Queued;
    markRangeMoving();
    carouselResult="Carousel position test requested.";
    web.send(202,"text/plain","Carousel test requested for 1 second. Inspect alignment before saving.");
  }
  void saveCarousel() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    uint16_t selected=0,pulse=0;
    if(servoFault||phase!=Phase::Idle||!pv_u16(web.arg("slot").c_str(),&selected)||selected<1||selected>SlotCount||
        !pv_u16(web.arg("pulse").c_str(),&pulse)||!validPulse(pulse)||
        selected!=carouselTestSlot||pulse!=carouselTestPulse||web.arg("verified")!="1"){
      web.send(400,"text/plain","Successfully test this compartment and pulse this session, then confirm the observed alignment.");return;
    }
    for(uint8_t i=0;i<SlotCount;++i)if(i!=selected-1&&carousel.pulses[i]==pulse){
      web.send(409,"text/plain","That pulse is assigned to another compartment. Test a distinct reachable position.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused after carousel settings changed.");
    CarouselSettings updated=carousel;updated.magic=CarouselMagic;updated.pulses[selected-1]=pulse;
    if(!saveBytes("carousel5",&updated,sizeof(updated))){requireStorage();return;}
    carousel=updated;
    carouselResult="Carousel position saved. Only calibrated reachable positions can be selected.";
    web.send(200,"text/plain",carouselResult);
  }
  void requestCarouselMove() {
    if(!authorize())return;noCache();
    if(!canCarouselMove(millis())){web.send(409,"text/plain",gateStatus(millis()));return;}
    uint16_t selected=0;
    if(!pv_u16(web.arg("slot").c_str(),&selected)||selected<1||selected>SlotCount||web.arg("clear")!="1"){
      web.send(400,"text/plain","Choose a compartment and confirm that the mechanism is clear.");return;
    }
    if(!carouselSlotSet(static_cast<uint8_t>(selected-1))){
      web.send(409,"text/plain","Test and save a reachable carousel position for this compartment first.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused for manual carousel positioning.");
    carouselSlot=static_cast<uint8_t>(selected-1);carouselCommandedSlot=0;
    motion=Motion::CarouselMove;phase=Phase::Queued;
    markRangeMoving();
    carouselResult="Flap closure then carousel position requested. Observe both movements.";
    web.send(202,"text/plain",carouselResult);
  }
  void saveWifi() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    const String ssid=web.arg("ssid"),password=web.arg("password");
    if(ssid.length()<1||ssid.length()>32||password.length()>63||(password.length()>0&&password.length()<8)){
      web.send(400,"text/plain","Wi-Fi name must be 1 to 32 bytes; password must be 8 to 63 characters, or empty for an open network.");return;
    }
    // Reject embedded NULs so a POST cannot silently store truncated credentials.
    if(strlen(ssid.c_str())!=ssid.length()||strlen(password.c_str())!=password.length()){
      web.send(400,"text/plain","Invalid Wi-Fi credentials.");return;
    }
    if(!consumeToken())return;
    NetworkSettings updated{};updated.magic=NetworkMagic;
    memcpy(updated.ssid,ssid.c_str(),ssid.length()+1);memcpy(updated.password,password.c_str(),password.length()+1);
    if(!saveBytes("network",&updated,sizeof(updated))){requireStorage();return;}
    network=updated;networkQueued=true;
    web.send(200,"text/plain","Wi-Fi saved. Connecting shortly; the ESP32 access point remains available. Check the connection address above.");
  }
  void saveTag() {
    if(!authorize())return;noCache();if(!requireStorage())return;
    uint16_t selected;String uid=web.arg("uid");uid.toUpperCase();
    if(!pv_u16(web.arg("slot").c_str(),&selected)||selected<1||selected>SlotCount||uid.length()>20||
        strlen(uid.c_str())!=uid.length()||(uid.length()&&!pv_uid_valid(uid.c_str()))){
      web.send(400,"text/plain","Select compartment 1 to 8 and use a UID of 8, 14 or 20 hexadecimal characters, or empty to clear.");return;
    }
    for(uint8_t i=0;i<SlotCount;++i)if(i!=selected-1&&uid.length()&&!strcmp(tags.uids[i],uid.c_str())){
      web.send(409,"text/plain","That UID is assigned to another compartment. Clear its previous assignment first.");return;
    }
    if(!consumeToken())return;
    pauseNfc("NFC dispensing paused after tag assignment changed.");
    TagSettings updated=tags;updated.magic=TagsMagic;
    memset(updated.uids[selected-1],0,21);memcpy(updated.uids[selected-1],uid.c_str(),uid.length());
    if(!saveBytes("tags",&updated,sizeof(updated))){requireStorage();return;}tags=updated;
    web.send(200,"text/plain","Tag assignment saved. NFC dispensing is paused; enable it after calibration, then remove and retap the card.");
  }
  void startWeb() {
    web.on("/",HTTP_GET,[this]{if(!authorize())return;noCache();web.send_P(200,"text/html; charset=utf-8",DashboardHtml);});
    web.on("/state",HTTP_GET,[this]{sendState();});
    web.on("/release",HTTP_POST,[this]{requestCycle();});
    web.on("/servo/test",HTTP_POST,[this]{requestTest();});
    web.on("/servo/save",HTTP_POST,[this]{saveServo();});
    web.on("/servo/disable",HTTP_POST,[this]{disableServo();});
    web.on("/carousel/test",HTTP_POST,[this]{requestCarouselTest();});
    web.on("/carousel/save",HTTP_POST,[this]{saveCarousel();});
    web.on("/carousel/move",HTTP_POST,[this]{requestCarouselMove();});
    web.on("/nfc/mode",HTTP_POST,[this]{setNfcMode();});
    web.on("/fill/capture",HTTP_POST,[this]{captureFill();});
    web.on("/fill/clear",HTTP_POST,[this]{clearFill();});
    web.on("/contents/capture",HTTP_POST,[this]{captureContents();});
    web.on("/contents/clear",HTTP_POST,[this]{clearContents();});
    web.on("/wifi",HTTP_POST,[this]{saveWifi();});
    web.on("/tag",HTTP_POST,[this]{saveTag();});
    web.onNotFound([this]{if(!authorize())return;noCache();web.send(404,"text/plain","Not found");});
    web.begin();
  }
  void printAccess() {
    Serial.println();Serial.println("PHARMAVAULT NFC + fill firmware 1");
    Serial.println("SG90 flap: GPIO3. SG92R carousel: GPIO5.");
    Serial.println("HC-SR04 TRIG: GPIO13. Level-reduced ECHO: GPIO14. Type u for ultrasonic diagnostics.");
    Serial.println("PHARMAVAULT - credentials are specific to this ESP32");
    Serial.print("Wi-Fi name: ");Serial.println(apSsid);
    Serial.print("Wi-Fi password: ");Serial.println(accessPassword);
    Serial.println("Dashboard: http://192.168.4.1");
    Serial.println("Dashboard username: demo");
    Serial.print("Dashboard password: ");Serial.println(accessPassword);
    if(!storageOk)Serial.println("Settings storage unavailable; password is temporary. Restart before saving settings or servo tests.");
    if(!apStarted)Serial.println("Access point failed to start. Restart the ESP32.");
    if(WiFi.status()==WL_CONNECTED){Serial.print("Same-Wi-Fi dashboard: http://");Serial.println(WiFi.localIP());}
  }
  void printRangeDiagnostics() {
    const uint32_t now=millis();
    char line[360];
    Serial.println("PHARMAVAULT NFC + fill firmware 1 - read-only diagnostics");
    Serial.println("TRIG=GPIO13 ECHO=GPIO14; ECHO must be reduced to ESP32-safe voltage.");
    snprintf(line,sizeof(line),"HC-SR04 status=%s echo_us=%lu raw_mm=%lu samples=%u median_mm=%u spread_mm=%u stable=%s",
      phase==Phase::Idle?rangeStatus:"Moving",static_cast<unsigned long>(rawPulseUs),static_cast<unsigned long>(rangeRawMm),
      unsigned(rangeSamples),unsigned(rangeMedianMm),unsigned(rangeSpread),rangeStable(now)?"yes":"no");
    Serial.println(line);
    if(rangeHasSample){
      snprintf(line,sizeof(line),"Last attempted sample age_ms=%lu fresh=%s",static_cast<unsigned long>(now-rangeAt),rangeFresh(now)?"yes":"no");
      Serial.println(line);
    }else Serial.println("No ultrasonic sample attempted yet.");
    snprintf(line,sizeof(line),"Triggers=%lu valid=%lu invalid=%lu empty_ref_mm=%u present_ref_mm=%u contents=%s",
      static_cast<unsigned long>(rangeTriggers),static_cast<unsigned long>(rangeValidCount),static_cast<unsigned long>(rangeInvalidCount),
      unsigned(contents.emptyMm),unsigned(contents.presentMm),contentsState(now));
    Serial.println(line);Serial.println(contentsText(now));
    const int percentage=fillPercentage(now);
    snprintf(line,sizeof(line),"Fill: empty_mm=%u full_mm=%u percent=%d (-1 means unavailable)",
      unsigned(fill.emptyMm),unsigned(fill.fullMm),percentage);
    Serial.println(line);Serial.println(fillStatus(now));
    Serial.println("IdleHigh: ECHO was high before triggering. NoPulse: no complete echo within 25000 us. OutOfRange: outside 20..3000 mm.");
  }
  void printNfcDiagnostics() {
    Serial.println("PHARMAVAULT NFC + fill firmware 1 - NFC diagnostics");
    Serial.print("NFC enabled: ");Serial.println(nfcEnabled?"yes":"no");
    Serial.println(nfcStatus(millis()));Serial.println(nfcLastAction);
    Serial.print("Card currently latched: ");Serial.println(heldNfcUid[0]?heldNfcUid:"none");
  }
 public:
  void begin() {
    // No servo PWM at boot, even with saved endpoints and carousel positions.
    pinMode(GatePwm,OUTPUT);digitalWrite(GatePwm,LOW);
    pinMode(CarouselPwm,OUTPUT);digitalWrite(CarouselPwm,LOW);
    pinMode(Trigger,OUTPUT);digitalWrite(Trigger,LOW);pinMode(Echo,INPUT);
    Serial.begin(115200);msp.setRxBufferSize(1024);msp.setTxBufferSize(256);
    msp.begin(9600,SERIAL_8N1,UartRx,UartTx);
    WiFi.mode(WIFI_AP_STA);WiFi.setAutoReconnect(true);
    loadSettings();
    // Include the full hardware MAC, since byte order can put the vendor prefix in its low bits.
    snprintf(apSsid,sizeof(apSsid),"PharmaVault-%012llX",static_cast<unsigned long long>(ESP.getEfuseMac()&0xFFFFFFFFFFFFULL));
    const IPAddress apIp(192,168,4,1),mask(255,255,255,0);
    const bool ipOk=WiFi.softAPConfig(apIp,apIp,mask);
    apStarted=ipOk&&WiFi.softAP(apSsid,accessPassword);
    startWeb();
    if(network.magic==NetworkMagic)WiFi.begin(network.ssid,network.password);
    printAccess();
  }
  void run() {
    // UART remains responsive throughout motion: retries never time out into a second cycle.
    pollUart();
    const uint32_t now=millis();static uint32_t lastUi=0;
    if(nfcEnabled&&!readerFresh(now))pauseNfc("Reader heartbeat lost. Check the link and enable NFC dispensing again.");
    // HTTP handlers and ultrasound reads remain paused during bounded servo PWM.
    if(phase!=Phase::Idle){runMotion();delay(1);return;}
    if(elapsed(now,lastUi,8000)){
      lastUi=now;sendLcd(servoFault?"DEMO_SERVO_FAULT":endpointsSet()&&storageOk?"DEMO_READY":"CALIBRATE",-1);
    }
    pollDistance(now);
    if(networkQueued){networkQueued=false;WiFi.disconnect(false,false);WiFi.begin(network.ssid,network.password);}
    const bool connected=WiFi.status()==WL_CONNECTED;
    if(connected&&!wasConnected){Serial.print("Same-Wi-Fi dashboard: http://");Serial.println(WiFi.localIP());}
    wasConnected=connected;
    // '?' prints access details; 'u'/'U' prints read-only ranging diagnostics, with no credentials or movement.
    if(Serial.available()){const int c=Serial.read();if(c=='?')printAccess();else if(c=='u'||c=='U')printRangeDiagnostics();else if(c=='n'||c=='N')printNfcDiagnostics();}
    web.handleClient();delay(1);
  }
};

PharmaVault cabinet;
void setup(){cabinet.begin();}
void loop(){cabinet.run();}
