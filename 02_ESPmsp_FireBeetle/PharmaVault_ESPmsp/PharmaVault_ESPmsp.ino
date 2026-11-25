/* Author: Xavier Poh (XP). FireBeetle UART-to-TCP bridge. Revised 2026-11-25. */
/* PHARMAVAULT wireless bridge 1 - FireBeetle 2 ESP32-E / DFR0654 V1.0.
 * Arduino-ESP32 core 3.3.12. Board: FireBeetle 2 ESP32-E (NOT ESP32-S3).
 * MSP430 P3.3 TX -> FireBeetle D11/GPIO16 RX2.
 * FireBeetle D10/GPIO17 TX2 -> MSP430 P3.4 RX. Common local GND.
 * The FireBeetle's printed TX/RX (GPIO1/3) are USB serial: do not use them.
 * USB serial 115200, Newline: SETUP wizard; ? status; CANCEL; FORGET.
 * UART protocol remains 9600 8N1, 3.3V. No sensor/motor connects here.
 * Authenticated TCP bridge through the main S3's protected PharmaVault AP.
 * A link failure discards queued/partial frames; MSP430 retries normally.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "PvTcpChannel.h"
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select FireBeetle 2 ESP32-E, not ESP32S3 Dev Module."
#endif

class PharmaVaultBridge {
  static constexpr uint8_t UartRx=16,UartTx=17;
  static constexpr uint32_t PairMagic=0x50564231;
  struct Pair {uint32_t magic;char ssid[33],secret[33];};
  enum class SetupStage:uint8_t {Command,Ssid,Password};
  HardwareSerial msp_{2};Preferences prefs_;PvTcpChannel link_;PvBridgeLine uartLine_;
  Pair pair_{};bool storageOk_=false,configured_=false,wasOnline_=false;
  SetupStage stage_=SetupStage::Command;
  char nextSsid_[33]{},console_[100]{};size_t consoleUsed_=0;bool consoleOverflow_=false;
  uint32_t connectAt_=0,wifiAt_=0,seenGeneration_=0,uartLineAt_=0;
  uint32_t uartFrames_=0,uartRejected_=0,uartDropped_=0;

  void status(){
    Serial.println("PHARMAVAULT wireless bridge 1");
    Serial.print("Paired SSID: ");Serial.println(configured_?pair_.ssid:"not configured");
    Serial.print("Wi-Fi: ");Serial.println(WiFi.status()==WL_CONNECTED?"connected":"disconnected");
    Serial.print("Bridge: ");Serial.println(link_.online()?"ONLINE":"OFFLINE");
    Serial.println("MSP UART: 9600 8N1; RX D11/GPIO16; TX D10/GPIO17.");
    Serial.printf("UART forwarded=%lu invalid=%lu offline/full drops=%lu; network RX=%lu TX=%lu rejected=%lu\n",
      static_cast<unsigned long>(uartFrames_),static_cast<unsigned long>(uartRejected_),static_cast<unsigned long>(uartDropped_),
      static_cast<unsigned long>(link_.receivedFrames()),static_cast<unsigned long>(link_.sentFrames()),static_cast<unsigned long>(link_.rejectedFrames()));
    Serial.println("Commands (Newline): SETUP, ?, CANCEL, FORGET. Password is never printed.");
  }
  void beginWifi(){
    link_.close();uartLine_.reset();
    WiFi.disconnect(false,false);connectAt_=millis()-2000;wifiAt_=millis();
    if(configured_){WiFi.begin(pair_.ssid,pair_.secret);Serial.print("Connecting to ");Serial.println(pair_.ssid);}
  }
  void consoleLine(){
    console_[consoleUsed_]=0;
    if(consoleOverflow_){Serial.println("Input too long; nothing saved. Try the current prompt again.");return;}
    if(!strcmp(console_,"CANCEL")){
      stage_=SetupStage::Command;memset(nextSsid_,0,sizeof(nextSsid_));Serial.println("Setup cancelled; existing saved pair retained.");beginWifi();return;
    }
    if(stage_==SetupStage::Ssid){
      const size_t length=strlen(console_);
      if(!pvBridgeSsidValid(console_)){Serial.println("Enter the exact PharmaVault- plus 12 uppercase hexadecimal digits printed by the main S3, or CANCEL.");return;}
      memcpy(nextSsid_,console_,length+1);stage_=SetupStage::Password;
      Serial.println("Type the main S3's Wi-Fi password (PharmaVault67), then Enter. It will not be echoed.");return;
    }
    if(stage_==SetupStage::Password){
      if(!pvBridgeSecretValid(console_)){Serial.println("Expected the S3's Wi-Fi password (8 to 32 characters, no spaces). Try again, or CANCEL.");return;}
      Pair updated{};updated.magic=PairMagic;memcpy(updated.ssid,nextSsid_,sizeof(updated.ssid));memcpy(updated.secret,console_,33);
      if(!storageOk_||prefs_.putBytes("pair1",&updated,sizeof(updated))!=sizeof(updated)){
        Serial.println("Pair was NOT saved: settings storage failed. Restart and retry SETUP.");memset(&updated,0,sizeof(updated));return;
      }
      pair_=updated;configured_=true;stage_=SetupStage::Command;memset(nextSsid_,0,sizeof(nextSsid_));
      Serial.println("Pair saved. Reconnecting; no source-code edits are needed.");beginWifi();return;
    }
    if(!strcmp(console_,"?")){status();return;}
    if(!strcmp(console_,"SETUP")){
      stage_=SetupStage::Ssid;link_.close();WiFi.disconnect(false,false);uartLine_.reset();
      Serial.println("Paste the main S3's PharmaVault-... Wi-Fi name, then Enter. CANCEL keeps the old pair.");return;
    }
    if(!strcmp(console_,"FORGET")){
      if(!storageOk_||!prefs_.remove("pair1")){Serial.println("No saved pair was removed. Use SETUP to enter a new pair.");return;}
      memset(&pair_,0,sizeof(pair_));configured_=false;beginWifi();Serial.println("Bridge pair removed. Main S3 settings are unchanged. Type SETUP to pair.");return;
    }
    if(consoleUsed_)Serial.println("Unknown command. Use SETUP or ? with Newline selected.");
  }
  void serviceConsole(){
    for(unsigned i=0;i<64&&Serial.available();++i){
      const int value=Serial.read();if(value=='\r')continue;
      if(value=='\n'){
        consoleLine();memset(console_,0,sizeof(console_));consoleUsed_=0;consoleOverflow_=false;
      }else if(value>=32&&value<=126&&consoleUsed_<sizeof(console_)-1&&!consoleOverflow_)console_[consoleUsed_++]=char(value);
      else consoleOverflow_=true;
    }
  }
  void serviceNetwork(uint32_t now){
    if(stage_!=SetupStage::Command||!configured_)return;
    if(WiFi.status()!=WL_CONNECTED){
      if(link_.attached())link_.close();
      if(now-wifiAt_>=5000){wifiAt_=now;WiFi.reconnect();}
      return;
    }
    if(!link_.attached()&&now-connectAt_>=2000){
      connectAt_=now;WiFiClient connection;
      // Only the sensor-side bridge waits here, bounded to 100ms. The motor S3
      // never makes outbound connections or waits for a client.
      if(connection.connect(IPAddress(192,168,4,1),PV_BRIDGE_PORT,100))link_.attach(connection,pair_.secret,false,now);
    }
    link_.poll(millis());
  }
  void serviceUart(){
    // Discard bytes that belonged to a previous link session, including a partial
    // UART line. MSP retries/periodic sensor reports supply fresh requests.
    if(seenGeneration_!=link_.generation()){
      seenGeneration_=link_.generation();uartLine_.reset();
      for(unsigned i=0;i<1024&&msp_.available();++i)msp_.read();
    }
    if(uartLine_.pending()&&millis()-uartLineAt_>=PV_BRIDGE_FRAME_TTL_MS){uartLine_.discardUntilNewline();uartLineAt_=millis();}
    for(unsigned i=0;i<256&&msp_.available();++i){
      if(!uartLine_.pending())uartLineAt_=millis();
      if(!uartLine_.push(msp_.read()))continue;
      uint8_t frame[PV_FRAME_SIZE];const size_t size=strlen(uartLine_.text());
      memcpy(frame,uartLine_.text(),size);frame[size]='\n';
      if(!pvBridgeFrameValid(frame,size+1)){++uartRejected_;continue;}
      if(link_.write(frame,size+1)==size+1)++uartFrames_;else ++uartDropped_;
    }
    const int count=link_.available();
    if(count>0&&count<int(PV_FRAME_SIZE)&&msp_.availableForWrite()>=count){
      uint8_t frame[PV_FRAME_SIZE];for(int i=0;i<count;++i)frame[i]=uint8_t(link_.read());
      if(msp_.write(frame,size_t(count))!=size_t(count)){link_.close();Serial.println("UART transmit failed; link reset to discard partial output.");}
    }
  }
 public:
  void begin(){
    Serial.begin(115200);msp_.setRxBufferSize(1024);msp_.setTxBufferSize(256);msp_.begin(9600,SERIAL_8N1,UartRx,UartTx);
    storageOk_=prefs_.begin("pvbridge",false);
    if(storageOk_&&prefs_.getBytesLength("pair1")==sizeof(pair_)&&prefs_.getBytes("pair1",&pair_,sizeof(pair_))==sizeof(pair_)){
      configured_=pair_.magic==PairMagic&&memchr(pair_.ssid,0,sizeof(pair_.ssid))&&pvBridgeSsidValid(pair_.ssid)&&
        memchr(pair_.secret,0,sizeof(pair_.secret))&&pvBridgeSecretValid(pair_.secret);
    }
    if(!configured_)memset(&pair_,0,sizeof(pair_));
    WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);beginWifi();status();
    if(!configured_)Serial.println("First use: type SETUP and follow the two prompts.");
  }
  void run(){
    serviceConsole();serviceNetwork(millis());serviceUart();
    const bool online=link_.online();
    if(online!=wasOnline_){Serial.println(online?"BRIDGE ONLINE - MSP430 frames can reach the main S3.":"BRIDGE OFFLINE - waiting to reconnect; old frames discarded.");wasOnline_=online;}
    delay(1);
  }
};
PharmaVaultBridge bridge;
void setup(){bridge.begin();}
void loop(){bridge.run();}
