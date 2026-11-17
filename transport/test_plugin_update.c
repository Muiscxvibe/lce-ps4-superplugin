#include "plugin_update.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *count_of(const char *h, const char *n, int *c) { *c = 0; for (const char *p = strstr(h, n); p; p = strstr(p + 1, n)) ++*c; return h; }
int main(int argc, char **argv) {
    assert(lce_plugin_build_version("r8-anticheat-20261117222032") == 20261117222032ull);
    assert(lce_plugin_build_version("dev") == 0 && lce_plugin_build_version("r8-x-2026111722203a") == 0);
    assert(lce_plugin_catalog_version("20261117222032") == 20261117222032ull);
    assert(lce_plugin_catalog_version("2026111722203") == 0 && lce_plugin_catalog_version("2026.1117222032") == 0);
    const char *ini = "[CUSA00265]\r\n/data/GoldHEN/plugins/mc195_lce_session_aaaa.prx=false\r\n/data/GoldHEN/plugins/mc195_lce_session_0123abcd.prx=true\r\n[default]\n/data/GoldHEN/plugins/gs_resident_plugin.prx=true\n";
    char out[1024]; size_t n = 0;
    assert(lce_plugin_ini_switch(ini, strlen(ini), "/data/GoldHEN/plugins/mc195_lce_session_ffff0000ffff0000.prx", out, sizeof out, &n));
    out[n] = 0;
    assert(strstr(out, "mc195_lce_session_0123abcd.prx=false\r\n/data/GoldHEN/plugins/mc195_lce_session_ffff0000ffff0000.prx=true\r\n[default]"));
    assert(strstr(out, "gs_resident_plugin.prx=true"));
    int c; count_of(out, ".prx=true", &c); assert(c == 2);
    /* zero or two active entries: refuse */
    const char *none = "[CUSA00265]\n/data/x/mc195_lce_session_aa.prx=false\n";
    assert(!lce_plugin_ini_switch(none, strlen(none), "/data/a.prx", out, sizeof out, &n));
    const char *two = "/d/mc195_lce_session_aa.prx=true\n/d/mc195_lce_session_bb.prx=true\n";
    assert(!lce_plugin_ini_switch(two, strlen(two), "/data/a.prx", out, sizeof out, &n));
    /* unsafe new paths / overflow */
    assert(!lce_plugin_ini_switch(ini, strlen(ini), "relative.prx", out, sizeof out, &n));
    assert(!lce_plugin_ini_switch(ini, strlen(ini), "/data/../x.prx", out, sizeof out, &n));
    assert(!lce_plugin_ini_switch(ini, strlen(ini), "/data/x.prx\n/evil.prx", out, sizeof out, &n));
    assert(!lce_plugin_ini_switch(ini, strlen(ini), "/data/GoldHEN/plugins/a.prx", out, 20, &n));
    uint8_t sha[32] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
    char path[128]; assert(lce_plugin_final_path(sha, path, sizeof path));
    assert(!strcmp(path, "/data/GoldHEN/plugins/mc195_lce_session_deadbeef01020304.prx"));
    if (argc > 1) { /* real console plugins.ini */
        FILE *f = fopen(argv[1], "rb"); static char buf[65536]; size_t m = fread(buf, 1, sizeof buf, f); fclose(f);
        static char o2[70000]; size_t n2;
        assert(lce_plugin_ini_switch(buf, m, path, o2, sizeof o2, &n2));
        assert(n2 > m); o2[n2] = 0; assert(strstr(o2, path)); 
    }
    puts("plugin update checks passed");
    return 0;
}
