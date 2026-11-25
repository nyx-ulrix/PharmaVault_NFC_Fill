/* Author: Xavier Poh (XP). Wireless transport (TCP channel, bridge core, wireless serial). Revised 2026-11-25. */
#ifndef PHARMAVAULT_TCP_CHANNEL_H
#define PHARMAVAULT_TCP_CHANNEL_H
#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#ifndef PV_BRIDGE_SOCKET_TEST
#include <lwip/sockets.h>
#endif
#include "PvBridgeCore.h"

// One connection, fixed queues, bounded reads and nonblocking socket sends.
// WiFiClient::write has a retry loop; deliberately use MSG_DONTWAIT here so
// a lost bridge cannot stall the S3's motor-step or dashboard service loop.
class PvTcpChannel {
  WiFiClient socket_;
  PvBridgeLine line_;
  PvBridgeQueue incoming_,outgoing_;
  const char *secret_=nullptr;
  bool server_=false,attached_=false,ready_=false;
  uint32_t startedAt_=0,lastRxAt_=0,lastPingAt_=0,generation_=0,lineAt_=0;
  uint32_t rejected_=0,received_=0,sent_=0;
  bool control(const char *text,uint32_t now){return outgoing_.push(reinterpret_cast<const uint8_t*>(text),strlen(text),now);}
  void lineReceived(uint32_t now){
    const char *line=line_.text();
    if(!ready_){
      if(server_){
        if(strncmp(line,"PVB1 ",5)||strcmp(line+5,secret_)){close();return;}
        if(!control("PVB1 READY\n",now)){close();return;}
      }else if(strcmp(line,"PVB1 READY")){close();return;}
      ready_=true;++generation_;lastRxAt_=lastPingAt_=now;return;
    }
    if(!strcmp(line,"PVB1 PING")){
      lastRxAt_=now;if(!control("PVB1 PONG\n",now))close();return;
    }
    if(!strcmp(line,"PVB1 PONG")){lastRxAt_=now;return;}
    const size_t size=strlen(line);
    uint8_t frame[PV_FRAME_SIZE];memcpy(frame,line,size);frame[size]='\n';
    if(!pvBridgeFrameValid(frame,size+1)){++rejected_;return;}
    lastRxAt_=now;
    if(!incoming_.push(frame,size+1,now)){++rejected_;return;}
    ++received_;
  }
 public:
  void attach(WiFiClient connection,const char *secret,bool server,uint32_t now){
    close();socket_=connection;secret_=secret;server_=server;attached_=true;
    startedAt_=lastRxAt_=lastPingAt_=now;socket_.setNoDelay(true);
    if(!server_){char hello[48];snprintf(hello,sizeof(hello),"PVB1 %s\n",secret_);if(!control(hello,now))close();}
  }
  void close(){
    if(attached_)++generation_;socket_.stop();attached_=ready_=false;
    line_.reset();incoming_.clear();outgoing_.clear();
  }
  bool attached()const{return attached_;}
  bool online()const{return attached_&&ready_;}
  uint32_t generation()const{return generation_;}
  uint32_t receivedFrames()const{return received_;}
  uint32_t sentFrames()const{return sent_;}
  uint32_t rejectedFrames()const{return rejected_;}
  int available(){return online()?incoming_.available():0;}
  int read(){return online()?incoming_.read():-1;}
  int availableForWrite()const{return online()&&outgoing_.room()?PV_FRAME_SIZE:0;}
  size_t write(const uint8_t *data,size_t length){
    if(!online()||!pvBridgeFrameValid(data,length)||!outgoing_.push(data,length,nowMs()))return 0;
    return length;
  }
  void poll(uint32_t now){
    if(!attached_)return;
    if(!socket_.connected()||socket_.fd()<0){close();return;}
    if((!ready_&&now-startedAt_>=PV_BRIDGE_AUTH_MS)||(ready_&&now-lastRxAt_>=PV_BRIDGE_LINK_TIMEOUT_MS)){close();return;}
    if(line_.pending()&&now-lineAt_>=PV_BRIDGE_FRAME_TTL_MS){close();return;}
    if(!incoming_.discardExpired(now)||!outgoing_.discardExpired(now)){close();return;}
    uint8_t bytes[128];const int count=::recv(socket_.fd(),bytes,sizeof(bytes),MSG_DONTWAIT);
    if(count==0){close();return;}
    if(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){close();return;}
    for(int i=0;i<count&&attached_;++i){
      if(!line_.pending())lineAt_=now;
      if(line_.push(bytes[i]))lineReceived(now);
    }
    if(!attached_)return;
    if(ready_&&!server_&&now-lastPingAt_>=PV_BRIDGE_HEARTBEAT_MS){
      if(control("PVB1 PING\n",now))lastPingAt_=now;
    }
    PvBridgeQueue::Frame *frame=outgoing_.front();if(!frame)return;
    const int sent=::send(socket_.fd(),frame->bytes+frame->offset,frame->size-frame->offset,MSG_DONTWAIT);
    if(sent<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)close();return;}
    if(!sent)return;
    frame->offset=uint16_t(frame->offset+sent);
    if(frame->offset==frame->size){if(frame->bytes[0]=='P'&&frame->bytes[1]=='V'&&frame->bytes[2]=='1')++sent_;outgoing_.pop();}
  }
};
#endif
