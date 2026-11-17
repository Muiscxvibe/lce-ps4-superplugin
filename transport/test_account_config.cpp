#include "account_config.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main() {
    lce_servers::List servers{};servers.count=1;
    strcpy(servers.entries[0].host,"192.168.1.165");servers.entries[0].port=25565;
    std::string valid="192.168.1.165\n25565\nhttps://192.168.1.165:25567/v1/auth\n"+std::string(64,'a')+"\n";
    lce_account_config::Config config{};
    auto accepts=[&](const std::string &s) {return lce_account_config::parse(s.data(),s.size(),servers,&config);};
    assert(accepts(valid));assert(!strcmp(config.url,"https://192.168.1.165:25567/v1/auth"));
    std::string s=valid;s.replace(0,13,"192.168.1.166");assert(!accepts(s));
    s=valid;s.replace(s.find("25565"),5,"25566");assert(!accepts(s));
    s=valid;s.replace(s.find("https"),5,"http");assert(!accepts(s));
    s=valid;s.insert(s.find("192.168.1.165:25567"),"user@");assert(!accepts(s));
    s=valid;s[s.size()-2]='g';assert(!accepts(s));
    s=valid+"extra\n";assert(!accepts(s));
    servers.count=2;assert(!accepts(valid));   // second entry has no host
    // Extra servers on the same host share the account endpoint.
    strcpy(servers.entries[1].host,"192.168.1.165");servers.entries[1].port=25570;
    servers.count=3;strcpy(servers.entries[2].host,"192.168.1.165");servers.entries[2].port=25571;
    assert(accepts(valid));
    // A server on another host must not inherit this endpoint.
    strcpy(servers.entries[2].host,"10.0.0.9");assert(!accepts(valid));
    strcpy(servers.entries[2].host,"192.168.1.16");assert(!accepts(valid));
    strcpy(servers.entries[2].host,"192.168.1.1650");assert(!accepts(valid));
}
