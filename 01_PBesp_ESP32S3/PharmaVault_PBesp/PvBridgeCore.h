#ifndef PHARMAVAULT_BRIDGE_CORE_H
#define PHARMAVAULT_BRIDGE_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "PvProtocol.h"

// Whole protocol frames only. No incomplete or old frame is retained for reconnect.
static const uint32_t PV_BRIDGE_FRAME_TTL_MS=3000;
static const uint32_t PV_BRIDGE_AUTH_MS=3000;
static const uint32_t PV_BRIDGE_HEARTBEAT_MS=2000;
static const uint32_t PV_BRIDGE_LINK_TIMEOUT_MS=8000;
static const uint16_t PV_BRIDGE_PORT=4210;

inline bool pvBridgeSecretValid(const char *s) {
  if(!s||strlen(s)!=32)return false;
  for(size_t i=0;i<32;++i)if(pv_hex(s[i])<0)return false;
  return true;
}
inline bool pvBridgeSsidValid(const char *s) {
  if(!s||strlen(s)!=24||strncmp(s,"PharmaVault-",12))return false;
  for(size_t i=12;i<24;++i)if(pv_hex(s[i])<0)return false;
  return true;
}
inline bool pvBridgeFrameValid(const uint8_t *data,size_t length) {
  if(!data||length<2||length>=PV_FRAME_SIZE||data[length-1]!='\n')return false;
  char line[PV_FRAME_SIZE];memcpy(line,data,length-1);line[length-1]=0;
  // Embedded NUL and CR are never part of the protocol's encoded frames.
  if(strlen(line)!=length-1)return false;
  char *fields[8];return pv_split(line,fields,8)>=3;
}

class PvBridgeLine {
  char bytes_[PV_FRAME_SIZE]{};size_t used_=0;bool discard_=false;
 public:
  void reset(){used_=0;discard_=false;bytes_[0]=0;}
  void discardUntilNewline(){used_=0;discard_=true;}
  bool pending()const{return used_||discard_;}
  const char *text()const{return bytes_;}
  size_t size()const{return used_;}
  // Caller consumes text immediately on true; next input begins a new line.
  bool push(int value){
    const char c=char(value);
    if(c=='\r')return false;
    if(c=='\n'){
      const bool complete=used_&&!discard_;bytes_[used_]=0;
      used_=0;discard_=false;return complete;
    }
    if(discard_)return false;
    if(value<32||value>126||used_>=sizeof(bytes_)-2){discard_=true;return false;}
    bytes_[used_++]=c;return false;
  }
};

class PvBridgeQueue {
 public:
  static const size_t Capacity=4;
  struct Frame {uint8_t bytes[PV_FRAME_SIZE];uint16_t size=0,offset=0;uint32_t at=0;};
 private:
  Frame frames_[Capacity]{};size_t head_=0,count_=0;
 public:
  void clear(){head_=count_=0;}
  bool room()const{return count_<Capacity;}
  bool empty()const{return !count_;}
  Frame *front(){return count_?&frames_[head_]:nullptr;}
  const Frame *front()const{return count_?&frames_[head_]:nullptr;}
  bool push(const uint8_t *data,size_t length,uint32_t now){
    if(!room()||!data||!length||length>=PV_FRAME_SIZE)return false;
    Frame &f=frames_[(head_+count_)%Capacity];memcpy(f.bytes,data,length);
    f.size=uint16_t(length);f.offset=0;f.at=now;++count_;return true;
  }
  void pop(){if(count_){head_=(head_+1)%Capacity;--count_;}}
  bool expired(uint32_t now)const{return count_&&now-front()->at>=PV_BRIDGE_FRAME_TTL_MS;}
  // Expired partially sent bytes require connection closure, not stream splicing.
  bool discardExpired(uint32_t now){
    while(expired(now)){if(front()->offset)return false;pop();}return true;
  }
  int available()const{return count_?int(front()->size-front()->offset):0;}
  int read(){if(!count_)return -1;Frame *f=front();const int c=f->bytes[f->offset++];if(f->offset==f->size)pop();return c;}
};
#endif
