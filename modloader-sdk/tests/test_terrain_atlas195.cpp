// Terrain atlas for SDK blocks (gap G1): the contract between the shipping
// terrain owner (r72 shared_terrain_195/runtime.cpp, N-owner form) and blocks195.
//
// runtime.cpp grows the stock terrain image by 2 + n tiles: cells 0/1 are the
// fixed Lucky/Steel tiles, cells 2.. are the SDK tiles that blocks195 composes
// (b195_terrain_compose, appended right after the fixed tiles in the same mip
// buffers) and later binds at first_index 2 (b195_terrain_bind). This test runs
// the REAL r72 layout/compose code (additive_terrain_lucky_195/layout.c) and the
// REAL blocks195 compose on the exact buffer packing runtime.cpp uses, and checks
// that every SDK sprite rectangle b195_terrain_bind computes is the cell the
// grown image carries that tile in, for every tile size (16/32/64 px packs) and
// count that fits the runtime's fixed tile buffers (2*256*256*4 bytes at mip 0).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "check.h"
#include "../modules/blocks195/blocks195.h"
extern "C" {
#include "../../r72-session-candidate/loader/additive_terrain_lucky_195/terrain.h"
}

namespace {
const size_t kTiles0 = 2u * 256 * 256 * 4;   // runtime.cpp tiles0
const unsigned kFixed = 2;

// runtime.cpp sdk_fit()
unsigned sdk_fit(unsigned n, unsigned size) {
    if (!n || n > LCE195_TERRAIN_TILES_MAX - 2u || (size_t)(2u + n) * size * size * 4u > kTiles0) return 0;
    return n;
}

b195_module g_m;
std::vector<std::vector<uint8_t>> g_src;

void make_module(unsigned n) {
    memset(&g_m, 0, sizeof g_m);
    g_src.assign(n, std::vector<uint8_t>(16 * 16 * 4));
    for (unsigned k = 0; k < n; ++k) {
        for (unsigned p = 0; p < 256; ++p) {
            g_src[k][p * 4 + 0] = (uint8_t)(k + 1);
            g_src[k][p * 4 + 1] = (uint8_t)p;
            g_src[k][p * 4 + 2] = (uint8_t)(0x40 + k);
            g_src[k][p * 4 + 3] = 0xff;
        }
        g_m.tiles[k].plan_texture = (int32_t)k;
        g_m.tiles[k].rgba = g_src[k].data();
    }
    g_m.tile_count = n;
}

// one size/count: pack like runtime.cpp make_tiles(), compose every mip with the real layout code
void run(unsigned size, unsigned n) {
    make_module(n);
    lce195_terrain_layout l;
    const uint32_t w = size * 16, stock = size * 39;
    if (lce195_terrain_layout_make(w, stock, size, kFixed + n, &l) != 1) { CHECK(!"layout refused"); return; }
    // b195_terrain_bind's own precondition on the grown height
    const uint32_t cols = w / size, last = kFixed + n - 1;
    CHECK(stock + (last / cols + 1) * size <= l.height);
    for (unsigned mip = 0; mip < 3; ++mip) {
        const unsigned side = size >> mip;
        const size_t one = (size_t)side * side * 4;
        std::vector<uint8_t> tiles(one * (kFixed + n), 0xee);   // fixed tiles: filler
        CHECK_EQ(b195_terrain_compose(&g_m, size, mip, tiles.data() + kFixed * one, one * n), 1);
        const uint32_t mw = w >> mip, mstock = stock >> mip, mh = l.height >> mip;
        std::vector<uint8_t> src((size_t)mw * mstock * 4, 0x11), dst((size_t)mw * mh * 4);
        CHECK_EQ(lce195_terrain_compose(&l, mip, src.data(), src.size(), tiles.data(), tiles.size(), dst.data(), dst.size()), 1);
        for (unsigned k = 0; k < n; ++k) {
            // the rectangle b195_terrain_bind gives SDK tile k (mip 0 units)
            const uint32_t idx = kFixed + k, bx = (idx % cols) * size, by = stock + (idx / cols) * size;
            uint32_t x = 0, y = 0;
            CHECK_EQ(lce195_terrain_position(&l, idx, &x, &y), 1);
            CHECK_EQ(x, bx);
            CHECK_EQ(y, by);
            // corner texels of that cell hold tile k (nearest-neighbour from its 16x16 source)
            const unsigned px[2] = {0, side - 1};
            for (unsigned a = 0; a < 2; ++a)
                for (unsigned b = 0; b < 2; ++b) {
                    const unsigned tx = px[a], ty = px[b], sx = tx * 16 / side, sy = ty * 16 / side;
                    const uint8_t *got = &dst[((size_t)((by >> mip) + ty) * mw + (bx >> mip) + tx) * 4];
                    if (memcmp(got, &g_src[k][(sy * 16 + sx) * 4], 4)) { CHECK(!"SDK tile texel misplaced"); return; }
                }
        }
        // the fixed cells keep the fixed tiles, the stock rows stay stock
        CHECK_EQ(dst[((size_t)(stock >> mip) * mw) * 4], 0xee);
        CHECK_EQ(dst[0], 0x11);
    }
}
}  // namespace

int main() {
    // budget: the fixed buffers hold 62 SDK tiles at 16/32 px, fewer at 64+ px, none at 256 px
    CHECK_EQ(sdk_fit(1, 16), 1u);
    CHECK_EQ(sdk_fit(62, 16), 62u);
    CHECK_EQ(sdk_fit(63, 16), 0u);              // LCE195_TERRAIN_TILES_MAX 64 = 2 fixed + 62
    CHECK_EQ(sdk_fit(62, 32), 62u);
    CHECK_EQ(sdk_fit(30, 64), 30u);
    CHECK_EQ(sdk_fit(31, 64), 0u);              // (2+31)*64*64*4 > 512 KiB
    CHECK_EQ(sdk_fit(6, 128), 6u);
    CHECK_EQ(sdk_fit(7, 128), 0u);
    // 128+ px packs: the stock rows alone exceed the 4096-row layout limit, so runtime.cpp grows nothing (reason 16)
    // for the fixed tiles either; the SDK tiles change nothing there.
    CHECK_EQ(sdk_fit(1, 256), 0u);              // the two fixed tiles already fill the buffer
    CHECK_EQ(sdk_fit(0, 16), 0u);
    lce195_terrain_layout l;
    CHECK_EQ(lce195_terrain_layout_make(256, 624, 16, 65, &l), 0);   // the real layout refuses more
    CHECK_EQ(lce195_terrain_layout_make(2048, 4992, 128, 2, &l), 0);
    unsigned runs = 0;
    for (unsigned size : {16u, 32u, 64u})
        for (unsigned n = 1; n <= 62; ++n)
            if (sdk_fit(n, size)) { run(size, n); ++runs; }
    CHECK_EQ(runs, 62u + 62u + 30u);
    // a non-power-of-two pack refuses the SDK compose (runtime.cpp then drops the SDK tiles for that generation)
    make_module(1);
    std::vector<uint8_t> t(24 * 24 * 4);
    CHECK_EQ(b195_terrain_compose(&g_m, 24, 0, t.data(), t.size()), 0);
    TEST_MAIN_END("test_terrain_atlas195");
}
