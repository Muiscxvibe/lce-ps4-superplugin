#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../ps4-toolchain/loader/server_config/servers.hpp"

namespace lce_account_config {
struct Config { char url[512]; char device[65]; };

// Four fixed lines: game host, game port, HTTPS URL and random device ID.
// The host/port must match the first server entry.
// Do not silently associate one server's account endpoint with another.
inline bool parse(const char *data,size_t size,const lce_servers::List &servers,Config *out) {
    if (!data || !out || !size || size>1024 || servers.count<1) return false;
    // Several servers may share this account endpoint only when they are on
    // the same host as the first entry (one machine, one account service).
    for (size_t k=1;k<servers.count;++k) {
        size_t i=0;
        for (; servers.entries[0].host[i] && servers.entries[k].host[i]==servers.entries[0].host[i]; ++i) {}
        if (servers.entries[0].host[i] || servers.entries[k].host[i]) return false;
    }
    const char *line[4]; size_t len[4]{}; size_t pos=0;
    for (unsigned i=0;i<4;++i) {
        line[i]=data+pos;
        while (pos<size && data[pos]!='\n') {
            unsigned char c=(unsigned char)data[pos];
            if (c<0x21 || c>0x7e) return false;
            ++pos;
        }
        len[i]=(size_t)(data+pos-line[i]);
        if (pos<size) ++pos; else if(i!=3) return false;
    }
    size_t hostlen=0;while(servers.entries[0].host[hostlen]) ++hostlen;
    if (pos!=size || len[0]!=hostlen || !len[1] || len[1]>5) return false;
    for(size_t i=0;i<hostlen;++i) if(line[0][i]!=servers.entries[0].host[i]) return false;
    unsigned port=0;
    for(size_t i=0;i<len[1];++i) {
        if(line[1][i]<'0'||line[1][i]>'9') return false;
        port=port*10+(unsigned)(line[1][i]-'0');
    }
    if(port!=servers.entries[0].port || len[2]<9 || len[2]>=sizeof out->url ||
       len[3]!=64) return false;
    for(size_t i=0;i<8;++i) if(line[2][i]!="https://"[i]) return false;
    // Reject URL credentials, fragments, backslashes and ambiguous authorities.
    for(size_t i=8;i<len[2];++i)
        if(line[2][i]=='@'||line[2][i]=='#'||line[2][i]=='\\') return false;
    if(line[2][8]=='/' || line[2][8]==':' || line[2][8]=='?') return false;
    for(size_t i=0;i<64;++i)
        if(!((line[3][i]>='0'&&line[3][i]<='9')||(line[3][i]>='a'&&line[3][i]<='f'))) return false;
    Config config{};
    for(size_t i=0;i<len[2];++i)config.url[i]=line[2][i];
    for(size_t i=0;i<64;++i)config.device[i]=line[3][i];
    *out=config; return true;
}
}
