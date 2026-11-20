// First-run self-setup (console side). Pure logic: ps4-transport/auto_config.c
// and lan_discovery.c (host tests: ps4-transport/run_test_selfsetup.sh).
//
// plugin_load -> lce_selfsetup_boot():
//   * creates the /data/lce-mods folders the content code expects (idempotent);
//   * writes /data/lce-account-ca.pem from the embedded public CA if missing;
//   * when /data/lce-servers.conf is missing or holds only auto-discovered
//     entries, starts a worker thread that, once the network is up, broadcasts
//     a LAN discovery query (UDP 25564) and writes the auto block from the
//     replies (or, if the file is missing and nothing answered, the built-in
//     public server). /data/lce-account.conf is created only when missing,
//     never changed afterwards.
// The owner tick holds the Join hooks back (lce_selfsetup_ready) until that
// first run finished (at most kGateMs), then reloads the list if it changed.
// Opening Join / Mini Games (lce_selfsetup_list_opened) re-runs discovery at
// most once a minute; a changed list applies at the next launch.
// Every step is logged; user-written server entries are never touched.
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <orbis/libkernel.h>
#include <orbis/Net.h>
#include <stdio.h>
#include <string.h>
#include "auto_config.h"
#include "client_report.h"
#include "lan_discovery.h"
#include "lce_default_ca.h"

extern "C" void mc195_remote_log(const char *);
extern "C" void lce_mods_notify(const char *message);
extern "C" int sysctl(const int *, unsigned, void *, size_t *, const void *, size_t);

namespace {
constexpr const char *kServers = "/data/lce-servers.conf";
constexpr const char *kServersTmp = "/data/lce-servers.conf.lce-auto";
constexpr const char *kAccount = "/data/lce-account.conf";
constexpr const char *kAccountTmp = "/data/lce-account.conf.lce-auto";
constexpr const char *kCa = "/data/lce-account-ca.pem";
constexpr const char *kCaTmp = "/data/lce-account-ca.pem.lce-auto";
constexpr const char *kDirs[] = {
    "/data/lce-mods", "/data/lce-mods/packs", "/data/lce-mods/downloads", "/data/lce-mods/downloads/texture-packs",
    "/data/lce-mods/downloads/maps", "/data/lce-mods/worlds", "/data/lce-mods/worlds/staged",
    "/data/lce-mods/reservations", "/data/GoldHEN/plugins/lce-update",
};
constexpr uint64_t kGateMs = 20000;          // Join hooks wait at most this long for the first run
constexpr uint64_t kNetWaitMs = 30000;       // give up waiting for the network after this
constexpr uint64_t kListedEveryMs = 60000;   // list-open rediscovery rate limit
constexpr unsigned kMaxReplies = 4;

unsigned g_need;          // 1 = this launch manages the server list
unsigned g_boot_done;     // first run finished (any outcome)
unsigned g_reload;        // list rewritten before the Join hooks were configured
unsigned g_request;       // list opened: rediscover
uint64_t g_boot_ms, g_last_list_ms;
OrbisPthread g_thread;
char g_conf[LCE_CONF_MAX_BYTES + 1], g_out[LCE_CONF_MAX_BYTES + 1];
lce_disc_reply g_replies[kMaxReplies];

void log(const char *m) { mc195_remote_log(m); }
uint64_t now_ms() { return sceKernelGetProcessTime() / 1000; }
bool exists(const char *p) { OrbisKernelStat st; return sceKernelStat(p, &st) >= 0; }

bool read_file(const char *path, char *buf, size_t cap, size_t *len) {
    int fd = sceKernelOpen(path, O_RDONLY | O_NOFOLLOW, 0);
    if (fd < 0) return false;
    size_t used = 0; bool ok = true;
    while (used < cap) {
        int64_t got = sceKernelRead(fd, buf + used, cap - used);
        if (got < 0) { ok = false; break; }
        if (!got) break;
        used += (size_t)got;
    }
    if (sceKernelClose(fd) < 0) ok = false;
    if (!ok || used == cap) return false;
    *len = used; return true;
}

bool write_replace(const char *tmp, const char *dest, const char *data, size_t len) {
    int fd = sceKernelOpen(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    bool ok = true;
    for (size_t off = 0; ok && off < len;) {
        int64_t w = sceKernelWrite(fd, data + off, len - off);
        if (w <= 0) ok = false; else off += (size_t)w;
    }
    if (ok && sceKernelFsync(fd) < 0) ok = false;
    if (sceKernelClose(fd) < 0) ok = false;
    if (ok) ok = sceKernelRename(tmp, dest) >= 0;
    if (!ok) (void)sceKernelUnlink(tmp);
    return ok;
}

void make_dirs() {
    unsigned made = 0;
    for (const char *d : kDirs)
        if (!exists(d)) { (void)sceKernelMkdir(d, 0777); if (exists(d)) ++made; }
    if (made) { char line[96]; snprintf(line, sizeof line, "Self-setup: created %u folder(s) under /data.\n", made); log(line); }
}

void write_ca_if_missing() {
    if (exists(kCa)) return;
    log(write_replace(kCaTmp, kCa, lce_default_ca_pem, sizeof lce_default_ca_pem - 1)
            ? "Self-setup: wrote /data/lce-account-ca.pem (built-in LCE account CA).\n"
            : "Self-setup: could not write /data/lce-account-ca.pem.\n");
}

// Random 64-hex device id: kernel arc4random (kern.arandom) mixed with the
// responder's seed, the TSC and the process clock, hashed.
void device_id(const char *seed, char out[65]) {
    uint8_t pool[32 + 64 + 24]{}; size_t have = 32;
    const int mib[2] = {1, 37};               // CTL_KERN, KERN_ARND
    (void)sysctl(mib, 2, pool, &have, nullptr, 0);
    size_t sl = strlen(seed); if (sl > 64) sl = 64;
    memcpy(pool + 32, seed, sl);
    uint64_t t[3] = {sceKernelReadTsc(), sceKernelGetProcessTime(), (uint64_t)(uintptr_t)&t};
    memcpy(pool + 96, t, sizeof t);
    uint8_t d[32]; lce_sha256(pool, sizeof pool, d);
    lce_hex64(d, out);
}

bool network_up() {
    int fd = sceNetSocket("lce_disc_check", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM, 0);
    if (fd < 0) return false;
    sceNetSocketClose(fd);
    return true;
}

void sockaddr_v4(uint8_t sa[16], const uint8_t ip[4], uint16_t port) {
    memset(sa, 0, 16); sa[0] = 16; sa[1] = 2; sa[2] = (uint8_t)(port >> 8); sa[3] = (uint8_t)port; memcpy(sa + 4, ip, 4);
}

// One discovery round: 3 broadcasts (plus a unicast to the last known host),
// replies collected for ~1.2 s. Returns the number of distinct hosts.
unsigned discover(const char *known_host) {
    int fd = sceNetSocket("lce_disc", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_DGRAM, 0);
    if (fd < 0) { log("Self-setup: discovery socket failed.\n"); return 0; }
    int one = 1;
    (void)sceNetSetsockopt(fd, 0xffff, 0x0020, &one, sizeof one);   // SO_BROADCAST
    (void)sceNetSetsockopt(fd, 0xffff, 0x1200, &one, sizeof one);   // SO_NBIO
    uint8_t bcast[4] = {255, 255, 255, 255}, known[4], sa[16];
    const bool have_known = known_host && *known_host && lce_disc_parse_ipv4(known_host, strlen(known_host), known);
    const uint32_t nonce = (uint32_t)(sceKernelReadTsc() ^ (sceKernelGetProcessTime() << 7));
    char q[32]; const size_t ql = lce_disc_format_query('Q', nonce, q, sizeof q);
    static char buf[LCE_DISC_MAX_DATAGRAM];
    unsigned hosts = 0;
    const uint64_t start = now_ms();
    for (unsigned sent = 0;;) {
        const uint64_t t = now_ms() - start;
        if (sent < 3 && t >= sent * 300ull) {
            sockaddr_v4(sa, bcast, LCE_DISC_PORT);
            (void)sceNetSendto(fd, q, ql, 0, (const OrbisNetSockaddr *)sa, 16);
            if (have_known) { sockaddr_v4(sa, known, LCE_DISC_PORT); (void)sceNetSendto(fd, q, ql, 0, (const OrbisNetSockaddr *)sa, 16); }
            ++sent;
        }
        uint8_t from[16]{}; OrbisNetSocklen_t fl = sizeof from;
        int n = sceNetRecvfrom(fd, buf, sizeof buf, 0, (OrbisNetSockaddr *)from, &fl);
        if (n > 0) {
            lce_disc_reply r;
            if (lce_disc_parse_reply(buf, (size_t)n, nonce, from + 4, &r) && r.count) {
                bool dup = false;
                for (unsigned i = 0; i < hosts; ++i) if (!strcmp(g_replies[i].host, r.host)) dup = true;
                if (!dup && hosts < kMaxReplies) g_replies[hosts++] = r;
            }
            continue;
        }
        if (t >= 1200) break;
        sceKernelUsleep(20000);
    }
    sceNetSocketClose(fd);
    char line[160];
    snprintf(line, sizeof line, "Self-setup: LAN discovery found %u server host(s)%s%s.\n", hosts,
             hosts ? ", first " : "", hosts ? g_replies[0].host : "");
    log(line);
    return hosts;
}

// boot: first run (may fall back to the public default and create account.conf)
// Returns true when lce-servers.conf was (re)written.
bool run(bool boot) {
    size_t len = 0;
    const bool have = read_file(kServers, g_conf, sizeof g_conf - 1, &len);
    lce_conf_info info; lce_conf_classify(have ? g_conf : nullptr, len, &info);
    if (have && (info.user_entries || info.malformed)) { log("Self-setup: lce-servers.conf has your own servers; not changing it.\n"); return false; }
    static char acct[1100]; size_t al = 0; char acct_host[64] = "";
    const bool have_acct = read_file(kAccount, acct, sizeof acct - 1, &al) && lce_account_conf_host(acct, al, acct_host, sizeof acct_host);
    const unsigned hosts = discover(info.auto_host[0] ? info.auto_host : have_acct ? acct_host : nullptr);
    int pick = lce_disc_pick(g_replies, hosts, have_acct ? acct_host : info.auto_host);
    lce_disc_reply fallback;
    const lce_disc_reply *reply = pick >= 0 ? &g_replies[pick] : nullptr;
    if (!reply && boot && !have) { lce_conf_default_reply(&fallback); reply = &fallback; log("Self-setup: nothing answered on the LAN; using the public LCE server.\n"); }
    if (!reply) return false;
    size_t out_len = 0;
    const int plan = lce_conf_merge(have ? g_conf : nullptr, len, reply, have_acct ? acct_host : nullptr, g_out, sizeof g_out - 1, &out_len);
    bool wrote = false;
    char line[200];
    if (plan == LCE_CONF_WRITE) {
        wrote = write_replace(kServersTmp, kServers, g_out, out_len);
        snprintf(line, sizeof line, wrote ? "Self-setup: wrote lce-servers.conf (%u server(s) on %s).\n" : "Self-setup: could not write lce-servers.conf (%u on %s).\n",
                 reply->count, reply->host);
        log(line);
    } else if (plan == LCE_CONF_UNCHANGED) log("Self-setup: lce-servers.conf already up to date.\n");
    else if (plan == LCE_CONF_OTHER_HOST) {
        snprintf(line, sizeof line, "Self-setup: found servers on %s but lce-account.conf names %s; list left unchanged.\n", reply->host, acct_host);
        log(line);
    } else if (plan == LCE_CONF_ERROR) log("Self-setup: server list too large; not written.\n");
    // account.conf: create only when missing, for the host the list now names.
    if (!have_acct && (plan == LCE_CONF_WRITE ? wrote : plan == LCE_CONF_UNCHANGED)) {
        uint16_t port = 0; char url[160]; char dev[65];
        (void)lce_conf_first_server(reply, &port);
        if (reply->account_url[0]) snprintf(url, sizeof url, "%s", reply->account_url);
        else (void)lce_default_account_url(reply->host, port, url, sizeof url);
        device_id(reply->seed, dev);
        size_t n = 0;
        if (lce_account_conf_format(reply->host, port, url, dev, acct, sizeof acct, &n) && write_replace(kAccountTmp, kAccount, acct, n)) {
            snprintf(line, sizeof line, "Self-setup: created lce-account.conf for %s (sign-in %s).\n", reply->host, url);
            log(line);
        } else log("Self-setup: could not create lce-account.conf.\n");
        memset(acct, 0, sizeof acct); memset(dev, 0, sizeof dev);
    }
    return wrote;
}

void *worker(void *) {
    const uint64_t start = now_ms();
    while (!network_up()) {
        if (now_ms() - start > kNetWaitMs) { log("Self-setup: network not ready; LAN discovery skipped.\n"); break; }
        sceKernelUsleep(250000);
    }
    if (network_up() && run(true)) __atomic_store_n(&g_reload, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_boot_done, 1u, __ATOMIC_RELEASE);
    for (;;) {
        sceKernelUsleep(250000);
        if (!__atomic_exchange_n(&g_request, 0u, __ATOMIC_ACQ_REL)) continue;
        if (run(false)) lce_mods_notify("LCE: found new servers on your network. Restart Minecraft to list them.");
    }
    return nullptr;
}
}  // namespace

// plugin_load, after the server list was read. Cheap: a few stat/mkdir calls.
extern "C" __attribute__((visibility("hidden"))) void lce_selfsetup_boot() {
    g_boot_ms = now_ms();
    make_dirs();
#if defined(LCE_OFFLINE_PREVIEW)
    __atomic_store_n(&g_boot_done, 1u, __ATOMIC_RELEASE);
    return;
#endif
    write_ca_if_missing();
    size_t len = 0;
    const bool have = read_file(kServers, g_conf, sizeof g_conf - 1, &len);
    lce_conf_info info; lce_conf_classify(have ? g_conf : nullptr, len, &info);
    if (have && (info.user_entries || info.malformed)) { g_boot_done = 1; log("Self-setup: using your lce-servers.conf.\n"); return; }
    g_need = 1;
    log(have ? "Self-setup: lce-servers.conf is auto-managed; refreshing from the LAN.\n"
             : "Self-setup: no lce-servers.conf; looking for LCE servers on the LAN.\n");
    if (scePthreadCreate(&g_thread, nullptr, worker, nullptr, "lce_selfsetup")) {
        g_boot_done = 1; log("Self-setup: worker thread failed.\n");
    }
}

// Owner tick: may the Join hooks / account route be configured now?
extern "C" __attribute__((visibility("hidden"))) bool lce_selfsetup_ready() {
    if (__atomic_load_n(&g_boot_done, __ATOMIC_ACQUIRE)) return true;
    static bool timed_out;
    if (!timed_out && now_ms() - g_boot_ms > kGateMs) { timed_out = true; log("Self-setup: still discovering; continuing without waiting.\n"); }
    return timed_out;
}

// Owner tick: true once when the first run rewrote lce-servers.conf.
extern "C" __attribute__((visibility("hidden"))) bool lce_selfsetup_take_reload() {
    return __atomic_exchange_n(&g_reload, 0u, __ATOMIC_ACQ_REL) != 0;
}

// Join list refresh / Mini Games menu (any thread): rediscover, rate-limited.
extern "C" void lce_selfsetup_list_opened() {
    if (!__atomic_load_n(&g_need, __ATOMIC_ACQUIRE) || !__atomic_load_n(&g_boot_done, __ATOMIC_ACQUIRE)) return;
    const uint64_t now = now_ms(), last = __atomic_load_n(&g_last_list_ms, __ATOMIC_ACQUIRE);
    if (last && now - last < kListedEveryMs) return;
    __atomic_store_n(&g_last_list_ms, now, __ATOMIC_RELEASE);
    __atomic_store_n(&g_request, 1u, __ATOMIC_RELEASE);
}
