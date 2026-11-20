#include <string.h>
#include "../loader/sha256.h"
#include "check.h"
static void hex(const uint8_t *d, char *out) { for (int i = 0; i < 32; ++i) sprintf(out + 2 * i, "%02x", d[i]); }
int main() {
    struct { const char *in; const char *want; } v[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    for (auto &t : v) { uint8_t d[32]; char h[65]; lce_sha256(t.in, strlen(t.in), d); hex(d, h); CHECK(!strcmp(h, t.want)); }
    // one million 'a' in uneven chunks
    lce_sha256_ctx c; lce_sha256_init(&c);
    static char buf[1000]; memset(buf, 'a', sizeof buf);
    size_t left = 1000000, step = 1;
    while (left) { size_t n = step < left ? step : left; lce_sha256_update(&c, buf, n > 1000 ? 1000 : n); left -= n > 1000 ? 1000 : n; step = step * 7 % 997 + 1; }
    uint8_t d[32]; char h[65]; lce_sha256_final(&c, d); hex(d, h);
    CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    TEST_MAIN_END("test_sha256");
}
