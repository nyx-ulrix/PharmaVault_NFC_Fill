/* Author: Clifton Ng Jun Heng (CN). Pillbox sensors: DHT11, NTC thermistor on the ADC, IR.
 * DHT11 bit timing is read by the Adafruit DHT sensor library (DHT.h/DHT.cpp, MIT licence, not our code).
 * Our code here: read scheduling, invalid-reading checks, scaling to x100 units and the humidity alert.
 * Archived team driver: Git revision ab0dd58, 04_WIRED_BACKUP/CCS/PharmaVault_MSP430/dht11.c (MSP430 Timer0_A input capture). */
#ifndef PHARMAVAULT_PILLBOX_SENSORS_H
#define PHARMAVAULT_PILLBOX_SENSORS_H
#include <Arduino.h>
#include <math.h>
#include "DHT.h"

// Local pillbox sensors, ESP32-S3 only. GPIO1 = ADC1_CH0; GPIO2 = DHT11 DATA.
// NTC: S3 GPIO13 driven HIGH -> fixed 10k -> GPIO1 junction -> 10k B3950 NTC -> GND.
// DHT: DATA GPIO2, VCC S3 3V3, GND S3 GND. NTC/DHT move off the MSP430.
// Call sample only while the motor is idle and no hand gesture is being timed.
// It briefly blocks for the DHT transaction. If true is returned, invalidate
// the IR observation/gesture so no hold is inferred across the unsampled gap.
class PvPillboxSensors {
 public:
  static constexpr uint8_t NtcPin=1,DhtPin=2;
  static constexpr uint32_t PeriodMs=5000,StartupMs=2000,FreshMs=16000;
  static constexpr int32_t InvalidTemperature=-32768;
  static constexpr uint16_t InvalidHumidity=65535;
  static constexpr int32_t MinDemoTemperature=2000,MaxDemoTemperature=3000;
  static constexpr uint16_t DefaultMaxHumidity=9000;  // %RH x100; top of the DHT11's rated 20-90 %RH; adjustable in Settings
  // Conversion assumes GPIO13 supplies nominally 3300 mV under the divider's light load.
  // Use calibrated ADC millivolts, not raw/4095 as a supply-voltage ratio.
  static constexpr float SupplyMillivolts=3300.0f,FixedOhms=10000.0f;
  // Calibrated 2026-11-26 (ML): B and R25 fitted to reference readings at 20, 25, 30 and 40 C
  // (datasheet values were R25 = 10000 ohm, B = 3950 K); error within +/-1 C from 20 to 40 C.
  static constexpr float NtcR25Ohms=9841.0f,NtcBeta=3998.0f;   // fitted calibration constants (ML)

  int32_t temperatureX100=InvalidTemperature;
  uint16_t rhX100=InvalidHumidity;
  int32_t dhtDiagnosticX100=InvalidTemperature;
  uint16_t ntcMillivolts=0;
  uint16_t ntcRaw12=0;   // Single diagnostic conversion, separate from the averaged mV readings.
  uint8_t dhtFrame[5]={};
  bool dhtFrameValid=false;
  uint16_t maxHumidity=DefaultMaxHumidity;
  uint8_t flags=4; // bit0 temperature warning; bit1 RH warning; bit2 sensor fault.
  uint32_t sampledAt=0;
  bool hasSample=false;

 private:
  DHT dht_{DhtPin,DHT11};
  uint32_t lastAt_=0;

 public:
  void begin(){
    pinMode(NtcPin,INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(NtcPin,ADC_11db);
    dht_.begin();lastAt_=nowMs();hasSample=false;
  }
  bool due(uint32_t now)const{return now-lastAt_>=(hasSample?PeriodMs:StartupMs);}
  bool fresh(uint32_t now)const{return hasSample&&now-sampledAt<FreshMs;}
  static int32_t ntcFromMillivolts(uint32_t mv){
    // The S3's calibrated 11dB range is approximately 0..3100mV. Reject near
    // either rail rather than turn a short/open or saturated ADC into a value.
    if(mv<50||mv>3050)return InvalidTemperature;
    const float resistance=FixedOhms*float(mv)/(SupplyMillivolts-float(mv));
    const float inverseKelvin=1.0f/298.15f+logf(resistance/NtcR25Ohms)/NtcBeta;
    const float celsius=1.0f/inverseKelvin-273.15f;
    if(!isfinite(celsius)||celsius<-40.0f||celsius>125.0f)return InvalidTemperature;
    return int32_t(lroundf(celsius*100.0f));
  }
  // Returns true only when hardware was sampled (even if a sample failed).
  // Caller may pass false when a motor or a hand-hold timing window is active.
  bool sample(uint32_t now,bool canBlock=true){
    if(!canBlock||!due(now))return false;
    ntcRaw12=analogRead(NtcPin);
    uint32_t sum=0;bool adcOk=true;
    for(uint8_t i=0;i<16;++i){
      const uint32_t mv=analogReadMilliVolts(NtcPin);
      if(mv<50||mv>3050)adcOk=false;
      sum+=mv;
    }
    const uint32_t average=(sum+8)/16;
    ntcMillivolts=uint16_t(average>65535?65535:average);
    temperatureX100=adcOk?ntcFromMillivolts(average):InvalidTemperature;

    const float rh=dht_.readHumidity();
    const float dhtTemperature=dht_.readTemperature(); // Same cached transaction.
    dhtFrameValid=dht_.copyLastFrame(dhtFrame);
    const bool dhtOk=isfinite(rh)&&isfinite(dhtTemperature)&&rh>=0.0f&&rh<=100.0f&&
                     dhtTemperature>=-20.0f&&dhtTemperature<=60.0f;
    rhX100=dhtOk?uint16_t(lroundf(rh*100.0f)):InvalidHumidity;
    dhtDiagnosticX100=dhtOk?int32_t(lroundf(dhtTemperature*100.0f)):InvalidTemperature;
    flags=0;
    if(temperatureX100==InvalidTemperature||rhX100==InvalidHumidity)flags|=4;
    if(temperatureX100!=InvalidTemperature&&(temperatureX100<MinDemoTemperature||temperatureX100>MaxDemoTemperature))flags|=1;
    if(rhX100!=InvalidHumidity&&rhX100>maxHumidity)flags|=2;
    sampledAt=lastAt_=nowMs();hasSample=true;
    return true;
  }
};
#endif
