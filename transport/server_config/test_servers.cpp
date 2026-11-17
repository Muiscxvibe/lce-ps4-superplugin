#include "servers.hpp"
#include <assert.h>
#include <string.h>

int main() {
    const char good[] = "# comment\n  LAN Server|192.168.1.165|25565  \nRemote|lce.example.net|1\n";
    lce_servers::List list = {};
    assert(lce_servers::parse(good, sizeof(good) - 1, &list) == lce_servers::Ok);
    assert(list.count == 2);
    assert(!strcmp(list.entries[0].label, "LAN Server"));
    assert(!strcmp(list.entries[0].host, "192.168.1.165"));
    assert(list.entries[0].port == 25565);
    assert(!strcmp(list.entries[1].host, "lce.example.net"));
    assert(list.entries[1].port == 1);

    const char *bad[] = {
        "bad|256.1.1.1|25565\n", "bad|host..name|25565\n",
        "bad|-host|25565\n", "bad|host|0\n", "bad|host|65536\n",
        "bad|host|1x\n", "bad|host|1|extra\n", "|host|1\n",
        "bad|host|1\na|host|2\nb|host|3\nc|host|4\nd|host|5\ne|host|6\nf|host|7\ng|host|8\nh|host|9\n"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(lce_servers::parse(bad[i], strlen(bad[i]), &list) != lce_servers::Ok);
    {   // minigame tag
        const char *t = "Main|192.168.1.165|25565\nBattle|192.168.1.165|25570|mg=1\n";
        assert(lce_servers::parse(t, strlen(t), &list) == lce_servers::Ok && list.count == 2);
        assert(list.entries[0].minigame == -1 && list.entries[1].minigame == 1 && list.entries[1].port == 25570);
        const char *bad_mg[] = {"A|1.2.3.4|5|mg=", "A|1.2.3.4|5|mg=256", "A|1.2.3.4|5|x=1", "A|1.2.3.4||mg=1", "A|1.2.3.4|5|mg=1a"};
        for (const char *b : bad_mg) assert(lce_servers::parse(b, strlen(b), &list) != lce_servers::Ok);
    }
    char oversized[lce_servers::kMaxFileBytes + 1] = {};
    assert(lce_servers::parse(oversized, sizeof(oversized), &list) == lce_servers::InputTooLarge);
    return 0;
}
