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
#include <esp_system.h>
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

#include "PvDashboardHtml.h"
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
  static constexpr uint32_t StaRetryMs=120000;uint32_t staRetryAt=0;WebServer web{80};DNSServer dns;PvWirelessSerial msp;Preferences prefs;
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
    if(prefs.getBytesLength("network")==sizeof(network))prefs.getBytes("network",&network,sizeof(network));{const uint16_t rh=prefs.getUShort("rhmax",PvPillboxSensors::DefaultMaxHumidity);if(rh>=3000&&rh<=9900)sensors.maxHumidity=rh;}
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
    snprintf(statusChunk,sizeof(statusChunk),"\"envFlags\":%u,\"tempMin\":%ld,\"tempMax\":%ld,\"rhMax\":%u,",unsigned(envFresh?environmentFlags:4),long(PvPillboxSensors::MinDemoTemperature),long(PvPillboxSensors::MaxDemoTemperature),unsigned(sensors.maxHumidity));json+=statusChunk;json+="\"homeSsid\":";appendQuoted(json,network.magic==NetworkMagic?network.ssid:"");json+=",";json+="\"refillInstructions\":[";
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
  void saveLimits(){if(!authorize())return;noCache();uint16_t pct=0;if(!pv_u16(web.arg("rhMax").c_str(),&pct)||pct<30||pct>99){web.send(400,"text/plain","Use a humidity limit from 30 to 99 %.");return;}if(!consumeToken())return;if(!storageOk||prefs.putUShort("rhmax",uint16_t(pct*100))!=2){requireStorage();return;}sensors.maxHumidity=uint16_t(pct*100);web.send(200,"text/plain","Humidity limit saved. Alerts now start above "+String(pct)+" %.");}
  void forgetWifi(){if(!authorize())return;noCache();if(!consumeToken())return;network=NetworkSettings{};if(storageOk)prefs.remove("network");WiFi.disconnect(false,true);web.send(200,"text/plain","Home network forgotten. The pillbox now uses only its own PharmaVault Wi-Fi.");}
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
    web.on("/refill/instructions",HTTP_POST,[this]{saveRefillInstructions();});web.on("/week/refill",HTTP_POST,[this]{refillWeek();});web.on("/wifi",HTTP_POST,[this]{saveWifi();});web.on("/wifi/forget",HTTP_POST,[this]{forgetWifi();});web.on("/limits",HTTP_POST,[this]{saveLimits();});
    web.onNotFound([this]{if(!authorize())return;noCache();web.send(404,"text/plain","Not found");});web.begin();
  }
  void printAccess(){Serial.println();Serial.println("PHARMAVAULT PBesp wireless 2 (local sensors)");Serial.println("ULN2003 IN1/2/3/4 = GPIO5/6/7/8; GPIO3 unused; IR OUT=GPIO14 at3.3V, LOW=hand, hold=1500ms.");Serial.println("Sensor link: FireBeetle wireless bridge on TCP 4210; S3 GPIO4/17 unused.");Serial.print("Wi-Fi name: ");Serial.println(apSsid);Serial.print("Wi-Fi password: ");Serial.println(accessPassword);Serial.println("Dashboard: http://pharmavault (or http://192.168.4.1)");Serial.println("Dashboard username: demo");Serial.print("Dashboard password: ");Serial.println(accessPassword);Serial.printf("Wi-Fi diag: channel=%u clients=%u/4 home network=%s (%s) uptime=%lus last reset=%d\n",unsigned(WiFi.channel()),unsigned(WiFi.softAPgetStationNum()),network.magic==NetworkMagic?network.ssid:"none saved",WiFi.status()==WL_CONNECTED?"connected":"not connected",static_cast<unsigned long>(millis()/1000),int(esp_reset_reason()));if(WiFi.status()==WL_CONNECTED){Serial.print("Same-Wi-Fi dashboard: http://pharmavault.local or http://");Serial.println(WiFi.localIP());}}
  void printDiagnostics(){char line[260];snprintf(line,sizeof(line),"Daily: clock=%s day=%lu minute=%u enabled=%s position=%s slot=%u IR=%d armed=%s",clockSynced?"set":"unset",static_cast<unsigned long>(localDay()),unsigned(localMinute()),scheduleEnabled?"yes":"no",positionKnown?"known":"unknown",unsigned(currentSlot),int(irRaw),handArmed?"yes":"no");Serial.println(line);Serial.println(systemStatus(millis()));Serial.println(lastAction);Serial.println(irTestStatus(millis()));Serial.println(weekStatus());}
 public:
  void begin(){
    pinMode(RetiredServo,INPUT);pinMode(Step1,OUTPUT);pinMode(Step2,OUTPUT);pinMode(Step3,OUTPUT);pinMode(Step4,OUTPUT);writeCoils(0);
    pinMode(RetiredTrigger,INPUT);pinMode(IrPin,INPUT_PULLUP);pinMode(UartRx,INPUT);pinMode(UartTx,INPUT);Serial.begin(115200);sensors.begin();
    loadSettings();WiFi.onEvent([](arduino_event_id_t e,arduino_event_info_t info){if(e==ARDUINO_EVENT_WIFI_AP_STACONNECTED)Serial.printf("WIFI client joined (aid %u)\n",unsigned(info.wifi_ap_staconnected.aid));else if(e==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)Serial.printf("WIFI client left (aid %u, reason %u)\n",unsigned(info.wifi_ap_stadisconnected.aid),unsigned(info.wifi_ap_stadisconnected.reason));else if(e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)Serial.printf("WIFI home network lost (reason %u)\n",unsigned(info.wifi_sta_disconnected.reason));else if(e==ARDUINO_EVENT_WIFI_STA_GOT_IP)Serial.println("WIFI home network connected");});WiFi.mode(WIFI_AP_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(false);snprintf(apSsid,sizeof(apSsid),"PharmaVault-%012llX",static_cast<unsigned long long>(ESP.getEfuseMac()&0xFFFFFFFFFFFFULL));
    const IPAddress ip(192,168,4,1),mask(255,255,255,0);apStarted=WiFi.softAPConfig(ip,ip,mask,IPAddress(uint32_t(0)),ip)&&WiFi.softAP(apSsid,accessPassword);if(apStarted)dns.start(53,"pharmavault",ip);if(MDNS.begin("pharmavault"))MDNS.addService("http","tcp",80);msp.begin(accessPassword);startWeb();if(network.magic==NetworkMagic)WiFi.begin(network.ssid,network.password);printAccess();
  }
  void run(){
    serviceClock(millis());pollUart();updateDeadlines();updateWeek();serviceTimerTest(millis());serviceProviderReport(millis());
    if(phase!=Phase::Idle){runMotion();delay(1);return;}
    pollIr(millis());servicePillboxSensors(millis());
    if(networkQueued){networkQueued=false;staRetryAt=millis();WiFi.disconnect(false,false);WiFi.begin(network.ssid,network.password);}
    const bool connected=WiFi.status()==WL_CONNECTED;if(connected&&!wasConnected){Serial.print("Same-Wi-Fi dashboard: http://pharmavault.local or http://");Serial.println(WiFi.localIP());}wasConnected=connected;
    // ponytail: one home-network attempt every 2 min; each attempt scans channels and briefly moves the PharmaVault AP.
    if(network.magic==NetworkMagic&&!connected&&millis()-staRetryAt>=StaRetryMs){staRetryAt=millis();WiFi.begin(network.ssid,network.password);}
    if(Serial.available()){const int c=Serial.read();if(c=='?')printAccess();else if(c=='i'||c=='I'||c=='n'||c=='N'||c=='u'||c=='U')printDiagnostics();}
    web.handleClient();serviceClock(millis());serviceDaily(millis());serviceHand(millis());delay(1);
  }
};
PharmaVault cabinet;
void setup(){cabinet.begin();}
void loop(){cabinet.run();}
