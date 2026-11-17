#include "player_send_hook.h"
#include <assert.h>
#include <stdio.h>
static int sends, failures, forwarded;
/* Isolate the outgoing route from network I/O. */
int lce_native_bridge_send(lce_native_bridge *b,const uint8_t *p,size_t n){(void)b;assert(p&&n==2);++sends;return sends==1;}
static void original(void *s,void *t,const void *p,int n,bool low,bool ack){(void)s;(void)t;(void)p;(void)n;assert(low&&ack);++forwarded;}
static void failed(void *p){(void)p;++failures;}
int main(void){int a,b,c;uint8_t bytes[]={1,2};lce_native_bridge bridge={0};lce_player_send_binding binding={&a,&b,&bridge,original,failed,0};
 assert(lce_player_send_install_binding(&binding));assert(!lce_player_send_install_binding(&binding));
 lce_player_send_hook(&a,&b,bytes,2,false,false);assert(sends==1&&!failures&&!forwarded);
 lce_player_send_hook(&a,&b,bytes,2,false,false);assert(sends==2&&failures==1&&!forwarded);
 lce_player_send_hook(&c,&b,bytes,2,true,true);assert(forwarded==1);
 lce_player_send_remove_binding(&binding);assert(lce_player_send_install_binding(&binding));lce_player_send_remove_binding(&binding);
 puts("player send routing tests passed");}
