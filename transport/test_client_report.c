#include "client_report.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    uint8_t d[32]; char h[65]; lce_sha256((const uint8_t *)"abc",3,d);
    for (int i=0;i<32;++i) sprintf(h+2*i,"%02x",d[i]);
    if (strcmp(h,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) { puts("sha256 FAIL"); return 1; }
    const char *mods[]={"eboot.bin","libc.prx"}; char out[2048]; const uint8_t key[32]={[0 ... 31]='k'};
    if (!lce_client_report_format(out,sizeof out,argc>1?argv[1]:"aaaa","new","01.95","t",mods,2,0,key,32)) { puts("format FAIL"); return 1; }
    puts(out); return 0;
}
