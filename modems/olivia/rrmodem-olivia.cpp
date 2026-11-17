/* RustyRig Olivia RX/TX worker. RX fd 4 at 16k, decoded text fd 1;
 * TX command fd 0, TX waveform fd 5 at 16k mono S16LE.
 * PTT lives in rrclient core and requires an explicit routed control pin.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <oliviamodem.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <vector>
#include <cstdint>
#include "../common/pcm16k.h"

static void feed(const float *buf,size_t n,void *ctx) {
   OliviaLib *rx=static_cast<OliviaLib *>(ctx);
   double pcm[1024];
   for(size_t i=0;i<n;i++) pcm[i]=buf[i];
   rx->feedAudio(pcm,(int)n,[](uint8_t c){
      std::putchar(c);
      std::fflush(stdout);
   });
}
static void *receive_thread(void *ctx) {
   rr_modem_rx_loop(feed,ctx);
   return nullptr;
}
static int16_t to_pcm(double v) {
   v=std::max(-1.,std::min(1.,v*.75));
   return static_cast<int16_t>(std::lrint(v*32767.));
}
int main(int argc,char **argv) {
   OliviaConfig cfg;
   if(argc>1) cfg.tones=std::atoi(argv[1]);
   if(argc>2) cfg.bandwidthHz=std::atoi(argv[2]);
   if(cfg.tones!=4 && cfg.tones!=8 && cfg.tones!=16 &&
      cfg.tones!=32 && cfg.tones!=64) return 2;
   if(cfg.bandwidthHz!=125 && cfg.bandwidthHz!=250 &&
      cfg.bandwidthHz!=500 && cfg.bandwidthHz!=1000 &&
      cfg.bandwidthHz!=2000) return 2;
   try {
      OliviaLib *rx=new OliviaLib(cfg); /* exit-time lifetime: detached RX */
      OliviaLib tx(cfg);
      std::setvbuf(stdout,nullptr,_IONBF,0);
      pthread_t receiver;
      if(pthread_create(&receiver,nullptr,receive_thread,rx)) return 1;
      pthread_detach(receiver);
      char command[512];
      size_t used=0,position=0,pending=0,sent=0;
      std::vector<double> waveform;
      int16_t out[320];
      double previous=0.;
      for(;;) {
         pollfd pfd={0,POLLIN,0};
         int rc=poll(&pfd,1,20);
         if(rc<0){if(errno==EINTR) continue;break;}
         if(pfd.revents&(POLLERR|POLLHUP|POLLNVAL)) break;
         if(rc>0 && (pfd.revents&POLLIN)) {
            char buf[256];
            ssize_t n=read(0,buf,sizeof(buf));
            if(n<=0){if(n<0&&errno==EINTR)continue;break;}
            for(ssize_t i=0;i<n;i++) {
               if(buf[i]=='\n') {
                  command[used]=0;
                  if(!std::strcmp(command,"CANCEL")) {
                     waveform.clear();
                     pending=sent=position=0;
                     previous=0.;
                  } else if(used>3&&!std::strncmp(command,"TX ",3)) {
                     waveform=tx.modulate(std::string(command+3,used-3),1500.);
                     pending=sent=position=0;
                     previous=0.;
                  }
                  used=0;
               } else if(buf[i]!='\r') {
                  if(used<sizeof(command)-1) command[used++]=buf[i];
                  else used=0;
               }
            }
         }
         if(pending) {
            ssize_t n=send(5,reinterpret_cast<char *>(out)+sent,
                           pending-sent,MSG_DONTWAIT|MSG_NOSIGNAL);
            if(n>0) sent+=static_cast<size_t>(n);
            else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)
               break;
            if(sent==pending) pending=sent=0;
         }
         if(!pending && position<waveform.size()) {
            size_t n=std::min<size_t>(160,waveform.size()-position);
            for(size_t i=0;i<n;i++) {
               double v=waveform[position+i];
               out[i*2]=to_pcm((previous+v)*.5);
               out[i*2+1]=to_pcm(v);
               previous=v;
            }
            position+=n;
            pending=2*n*sizeof(int16_t);
            sent=0;
         }
         if(!pending&&!waveform.empty()&&position==waveform.size()) {
            /* Separate control socket: never splice status into received text. */
            static const char done[]="TX_DONE";
            send(3,done,sizeof(done)-1,MSG_DONTWAIT|MSG_NOSIGNAL);
            waveform.clear();
            position=0;
         }
      }
      /* Child exit terminates the RX worker. Do not destroy its context
       * while the detached reader may still be inside feedAudio. */
      return 0;
   } catch(const std::exception &e) {
      std::fprintf(stderr,"Olivia: %s\n",e.what());
      return 1;
   }
}
