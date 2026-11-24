#ifndef PHARMAVAULT_WIRELESS_SERIAL_H
#define PHARMAVAULT_WIRELESS_SERIAL_H
#include "PvTcpChannel.h"

// Main ESP32-S3 side: replacement for its MSP-facing HardwareSerial only.
// The dashboard's HTTP service, AP settings and saved daily timer are untouched.
class PvWirelessSerial {
  WiFiServer server_{IPAddress(192,168,4,1),PV_BRIDGE_PORT,2};
  PvTcpChannel channel_;
  const char *secret_=nullptr;bool started_=false;uint32_t listenAt_=0;
  void startListener(){
    listenAt_=millis();server_.begin();server_.setNoDelay(true);started_=bool(server_);
    if(!started_)Serial.println("Wireless bridge listener unavailable; retrying in 3 seconds.");
  }
 public:
  void begin(const char *secret){
    secret_=secret;if(!pvBridgeSecretValid(secret_))return;
    startListener();
  }
  void poll(){
    if(!started_){if(pvBridgeSecretValid(secret_)&&millis()-listenAt_>=3000)startListener();return;}
    const uint32_t now=millis();channel_.poll(now);
    WiFiClient pending=server_.accept();
    if(pending){
      const IPAddress ip=pending.remoteIP();
      if(channel_.attached()||ip[0]!=192||ip[1]!=168||ip[2]!=4||ip[3]<2||ip[3]==255)pending.stop();
      else channel_.attach(pending,secret_,true,now);
    }
  }
  bool online()const{return channel_.online();}
  uint32_t generation()const{return channel_.generation();}
  int available(){return channel_.available();}
  int read(){return channel_.read();}
  int availableForWrite(){return channel_.availableForWrite();}
  size_t write(const uint8_t *data,size_t size){return channel_.write(data,size);}
};
#endif
