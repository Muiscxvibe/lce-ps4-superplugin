#include "../integration/viewport_tint_config.hpp"
#include "check.h"
#include "mock_backend.h"
#include <fstream>
#include <iterator>
#include <vector>

static lce_status accepted(const lce_api *,lce_mod_ctx *) { return LCE_OK; }
static lce_status rejected(const lce_api *,lce_mod_ctx *) { return LCE_E_INVALID; }

int main(int argc,char **argv) {
    CHECK_EQ(argc,3);
    if (argc != 3) return 1;
    lce_viewport_tint_v1 color{};
    CHECK(lce_viewport_tint_parse_v1("#0AaBcD60",&color));
    CHECK_EQ(color.red,10);
    CHECK_EQ(color.green,171);
    CHECK_EQ(color.blue,205);
    CHECK_EQ(color.alpha,96);
    const char *invalid_values[]={nullptr, "", "#", "#aa00bb6", "#aa00gg10", "#aa00bb61", "#aa00bb60 ", "aa00bb60"};
    for (const char *invalid : invalid_values) {
        CHECK(!lce_viewport_tint_parse_v1(invalid,&color));
        CHECK_EQ(color.alpha,96);
    }
    CHECK(!lce_viewport_tint_parse_v1("#F2B29223",nullptr));
    CHECK(lce_viewport_tint_parse_v1("#ABCDEF00",&color));
    CHECK_EQ(color.alpha,0);

    std::ifstream package_file(argv[1],std::ios::binary);
    std::ifstream config_file(argv[2],std::ios::binary);
    std::vector<uint8_t> package{std::istreambuf_iterator<char>(package_file),{}};
    std::string config{std::istreambuf_iterator<char>(config_file),{}};
    MockGame game{};
    mock_game_init(&game,0);
    auto backend=mock_backend(&game);
    lce_loader_config cfg{};
    cfg.backend=&backend;
    lce_loader *loader=lce_loader_create(&cfg);
    CHECK(loader);
    if (!loader) return 1;
    CHECK_EQ(lce_loader_add_native(loader,"zz_later","1.0",accepted),LCE_OK);
    CHECK_EQ(lce_loader_add_native(loader,"aa_rejected","1.0",rejected),LCE_OK);
    CHECK_EQ(lce_loader_add_native(loader,"ab_invalid","1.0",accepted),LCE_OK);
    CHECK_EQ(lce_loader_add_native(loader,"ac_disabled","1.0",accepted),LCE_OK);
    CHECK_EQ(lce_loader_add_native(loader,"ad_wrong_type","1.0",accepted),LCE_OK);
    CHECK_EQ(lce_loader_add_package(loader,package.data(),package.size(),"fixture"),LCE_OK);
    const char *values[]={
        "lce_visual.viewport_tint_v1 = \"#12345620\"\n",
        "lce_visual.viewport_tint_v1 = \"#00FF2010\"\n",
        "lce_visual.viewport_tint_v1 = \"#00FF2080\"\n",
        "lce_visual.viewport_tint_v1 = \"#00FF2000\"\n",
        "lce_visual.viewport_tint_v1 = 1\n",
        config.c_str()
    };
    for (uint32_t i=0;i<sizeof values/sizeof values[0];++i) {
        CHECK_EQ(lce_loader_set_config_text(loader,i,values[i],strlen(values[i])),LCE_OK);
    }
    CHECK_EQ(lce_viewport_tint::committed_color(nullptr),0);
    CHECK_EQ(lce_viewport_tint::committed_color(loader),0);
    CHECK_EQ(lce_loader_discover(loader),LCE_OK);
    CHECK_EQ(lce_viewport_tint::committed_color(loader),0);
    CHECK_EQ(lce_loader_commit(loader),LCE_OK);
    CHECK_EQ(lce_viewport_tint::committed_color(loader),0x22447740u);
    lce_loader_destroy(loader);
    TEST_MAIN_END("Viewport tint config");
}
