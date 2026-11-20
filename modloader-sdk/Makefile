# Host build + tests (Linux g++), and the PS4 static library (OpenOrbis clang).
#   make test      build example/fixture packages, run all host tests under ASan/UBSan
#   make ps4       build build/ps4/liblcemodloader.a for the PS4 toolchain
CXX ?= g++
CC ?= gcc
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -Werror $(SAN)
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror $(SAN)
B = build/host
CORE = loader/package.cpp loader/ledger.cpp loader/loader.cpp loader/ext.cpp
ITEMS195 = modules/items195/items.cpp modules/items195/recipes.cpp modules/items195/furnace.cpp modules/items195/effects.cpp modules/items195/adapter.cpp
PKGS = build/pkg/newretail/mod.lcm build/pkg/allcontent/mod.lcm build/pkg/retailall/mod.lcm build/pkg/rubyretail/mod.lcm build/pkg/rubymod/mod.lcm build/pkg/gem/mod.lcm build/pkg/rubytools/mod.lcm build/pkg/orphan/mod.lcm build/pkg/badver/mod.lcm build/pkg/blockstates/mod.lcm build/pkg/throwables/mod.lcm build/pkg/boats/mod.lcm build/pkg/shapes/mod.lcm build/pkg/fueltest/mod.lcm build/pkg/luafuel/mod.lcm
PKGS += build/pkg/luamobs/mod.lcm build/pkg/luatexture/mod.lcm build/pkg/luaworldgen/mod.lcm build/pkg/potiononly/mod.lcm

all: test

loader/vanilla_195.inc: tools/data/vanilla_195.json tools/gen_vanilla.py
	python3 tools/gen_vanilla.py

build/pkg/rubymod/mod.lcm: tools/lce_port.py tools/lce_modpack.py tools/port_compat.py $(shell find examples/java-rubymod -type f)
	python3 tools/lce_port.py java examples/java-rubymod/src build/port/rubymod --hints examples/java-rubymod/hints.json
	python3 tools/lce_modpack.py build build/port/rubymod build/pkg
build/pkg/gem/mod.lcm: tools/lce_port.py tools/lce_modpack.py tools/port_compat.py $(shell find examples/bedrock-sapphire -type f)
	python3 tools/lce_port.py bedrock examples/bedrock-sapphire/BP examples/bedrock-sapphire/RP build/port/gem
	python3 tools/lce_modpack.py build build/port/gem build/pkg
build/pkg/%/mod.lcm: tests/fixtures/%/mod.json $(wildcard tests/fixtures/*/scripts/*.lua) $(wildcard tests/fixtures/*/textures/*.png) tools/lce_modpack.py tools/lce_ext_schema.py
	python3 tools/lce_modpack.py build tests/fixtures/$* build/pkg

$(B)/sha256.o: loader/sha256.c loader/sha256.h
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/oneshot195.o: loader/retail195/oneshot195.c loader/retail195/oneshot195.h
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/test_oneshot: tests/test_oneshot.cpp $(B)/oneshot195.o $(B)/sha256.o
	$(CXX) $(CXXFLAGS) $^ -o $@
$(B)/test_config: tests/test_config.cpp loader/config_store.h tests/check.h
	@mkdir -p $(B); $(CXX) $(CXXFLAGS) tests/test_config.cpp -o $@
$(B)/test_fuel195: modules/fuel195/tests/test_fuel195.cpp modules/fuel195/fuel195.cpp modules/fuel195/adapter.cpp modules/fuel195/fuel195.h tests/check.h
	@mkdir -p $(B); $(CXX) $(CXXFLAGS) modules/fuel195/tests/test_fuel195.cpp modules/fuel195/fuel195.cpp modules/fuel195/adapter.cpp -o $@
$(B)/test_mob_spawn_route195: tests/test_mob_spawn_route195.cpp integration/r72_world_bind195.cpp modules/entities195/entities195.h tests/check.h
	@mkdir -p $(B); $(CXX) $(CXXFLAGS) tests/test_mob_spawn_route195.cpp integration/r72_world_bind195.cpp -o $@
$(B)/test_ext: tests/test_ext.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) tests/test_ext.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o -o $@
$(B)/test_sha256: tests/test_sha256.cpp $(B)/sha256.o
	$(CXX) $(CXXFLAGS) $^ -o $@
$(B)/test_package: tests/test_package.cpp $(CORE) $(B)/sha256.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) tests/test_package.cpp $(CORE) $(B)/sha256.o -o $@
$(B)/test_ledger: tests/test_ledger.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) tests/test_ledger.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o -o $@
$(B)/test_loader: tests/test_loader.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) tests/test_loader.cpp tests/mock_backend.cpp $(CORE) $(B)/sha256.o -o $@
$(B)/test_retail195: tests/test_retail195.cpp loader/retail195/backend195.cpp tests/mock_backend.cpp $(CORE) $(ITEMS195) $(B)/sha256.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) -DLCE195_HOST_TEST tests/test_retail195.cpp loader/retail195/backend195.cpp tests/mock_backend.cpp $(CORE) $(ITEMS195) $(B)/sha256.o -o $@

# Composite retail195 backend end-to-end (fake image; every module linked, Lua included).
MODULES = $(ITEMS195) modules/blocks195/blocks195.cpp modules/entities195/entities195.cpp modules/worldgen195/worldgen195.cpp \
          modules/events195/ev195.cpp modules/events195/services195.cpp modules/ui195/ui195.cpp \
          modules/strings195/strings195.cpp modules/enchants195/enchants195.cpp modules/effects195/effects195.cpp \
          modules/biomes195/biomes195.cpp modules/audio195/audio195.cpp modules/projectiles195/projectiles195.cpp modules/fuel195/fuel195.cpp modules/fuel195/adapter.cpp
COMPOSITE = loader/retail195/backend195.cpp loader/retail195/composite195.cpp loader/retail195/services_glue195.cpp
LUA_SRC = $(filter-out modules/script/lua/lua.c modules/script/lua/luac.c,$(wildcard modules/script/lua/*.c))
LUA_OBJ = $(patsubst modules/script/lua/%.c,$(B)/lua/%.o,$(LUA_SRC))
$(B)/lua/%.o: modules/script/lua/%.c
	@mkdir -p $(B)/lua; $(CC) -std=c11 -O1 -g $(SAN) -DLUA_USE_POSIX -w -c $< -o $@
$(B)/lce_script.o: modules/script/lce_script.c modules/script/lce_script.h include/lce_mod_api.h include/lce_tick_pacing.h
	@mkdir -p $(B); $(CC) $(CFLAGS) -DLUA_USE_POSIX -c $< -o $@
$(B)/test_composite195: tests/test_composite195.cpp tests/fake_effects195.h tests/mock_backend.cpp $(COMPOSITE) $(MODULES) $(CORE) $(B)/sha256.o $(B)/lce_script.o $(LUA_OBJ) loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) -DLCE195_HOST_TEST -DLCE195C_HOST_TEST -DLE195_TEST tests/test_composite195.cpp tests/mock_backend.cpp $(COMPOSITE) $(MODULES) $(CORE) $(B)/sha256.o $(B)/lce_script.o $(LUA_OBJ) -lm -o $@

R72 = /home/codey/minecraft-lce-work/r72-session-candidate/loader
R72_SEAMS = $(R72)/super_content_steel_nbt_save_195/lucky_items_bridge.cpp $(R72)/additive_items_195/window.cpp
$(B)/r72_catalog.o: $(R72)/additive_items_195/catalog.c
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/r72_atlas.o: $(R72)/additive_items_195/atlas.c
	@mkdir -p $(B); $(CC) $(CFLAGS) -Wno-misleading-indentation -c $< -o $@
$(B)/r72_assets.o: $(R72)/steel_tools_195/assets.c
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/r72_uv_plan.o: $(R72)/steel_gameplay/uv_plan_candidate/uv_plan.c
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
# The REAL shipping item-atlas owner (resource.cpp) in SDK-only / Steel / Lucky-alone sessions.
$(B)/test_item_atlas195: tests/test_item_atlas195.cpp tests/host-include/orbis/libkernel.h $(R72)/super_content_steel_nbt_save_195/resource.cpp $(B)/r72_atlas.o $(B)/r72_catalog.o $(B)/r72_uv_plan.o
	$(CXX) $(CXXFLAGS) -Wno-misleading-indentation -Wno-unused-function -DSTEEL_RESOURCE_TEST -Itests/host-include $(R72)/super_content_steel_nbt_save_195/resource.cpp tests/test_item_atlas195.cpp $(B)/r72_atlas.o $(B)/r72_catalog.o $(B)/r72_uv_plan.o -o $@
# Terrain atlas for SDK blocks (gap G1): the real r72 terrain layout/compose + blocks195 compose/bind geometry.
$(B)/r72_terrain_layout.o: $(R72)/additive_terrain_lucky_195/layout.c
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/test_terrain_atlas195: tests/test_terrain_atlas195.cpp modules/blocks195/blocks195.cpp modules/blocks195/blocks195.h $(B)/r72_terrain_layout.o
	$(CXX) $(CXXFLAGS) tests/test_terrain_atlas195.cpp modules/blocks195/blocks195.cpp $(B)/r72_terrain_layout.o -o $@
# Uses the real (read-only) r72 bridge/window/catalog sources; skipped if absent.
$(B)/test_retail195_bridge: tests/test_retail195_bridge.cpp loader/retail195/backend195.cpp $(CORE) $(ITEMS195) $(B)/sha256.o $(B)/r72_catalog.o $(B)/r72_atlas.o $(B)/r72_assets.o loader/vanilla_195.inc
	$(CXX) $(CXXFLAGS) -Wno-misleading-indentation -DLCE195_HOST_TEST tests/test_retail195_bridge.cpp loader/retail195/backend195.cpp $(CORE) $(ITEMS195) $(R72_SEAMS) $(B)/sha256.o $(B)/r72_catalog.o $(B)/r72_atlas.o $(B)/r72_assets.o -o $@

HOST_TESTS = $(B)/test_sha256 $(B)/test_package $(B)/test_ledger $(B)/test_loader $(wildcard tests/test_retail195.cpp)
test: $(PKGS) $(B)/test_sha256 $(B)/test_package $(B)/test_ledger $(B)/test_loader $(B)/test_oneshot $(B)/test_ext $(B)/test_composite195 $(B)/test_config $(B)/test_fuel195 $(B)/test_mob_spawn_route195
	$(B)/test_sha256
	$(B)/test_fuel195
	$(B)/test_mob_spawn_route195
	@if [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then python3 -I modules/fuel195/tests/test_image_pins.py; else echo "retail image absent: skipping fuel195 image pins"; fi
	@if [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ] && [ -x /home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python ]; then /home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python -I modules/fuel195/tests/test_retail_tick.py; else echo "retail image or analysis venv absent: skipping fuel195 tick"; fi
	$(B)/test_config build/pkg/allcontent/config
	$(B)/test_ext build/pkg
	$(B)/test_oneshot
	$(B)/test_package build/pkg/rubymod/mod.lcm build/pkg/throwables/mod.lcm build/pkg/boats/mod.lcm
	$(B)/test_ledger
	$(B)/test_loader build/pkg
	@if [ -f tests/test_retail195.cpp ]; then $(MAKE) -s $(B)/test_retail195 && $(B)/test_retail195 build/pkg; fi
	@if [ -d $(R72) ]; then $(MAKE) -s $(B)/test_retail195_bridge && $(B)/test_retail195_bridge build/pkg/rubyretail/mod.lcm; else echo "r72 tree absent: skipping real-bridge test"; fi
	@if [ -d $(R72) ]; then $(MAKE) -s $(B)/test_item_atlas195 && $(B)/test_item_atlas195; else echo "r72 tree absent: skipping item-atlas test"; fi
	@if [ -d $(R72) ]; then $(MAKE) -s $(B)/test_terrain_atlas195 && $(B)/test_terrain_atlas195; else echo "r72 tree absent: skipping terrain-atlas test"; fi
	$(B)/test_composite195 build/pkg
	python3 -I tests/test_tools.py
	@$(MAKE) -s modules-test
	@echo ALL HOST TESTS PASSED

# Every module's own suite. Optional parts (retail image, unicorn/capstone venv,
# OpenOrbis toolchain) are skipped inside the scripts when absent.
modules-test:
	@for m in blocks195 items195 entities195 worldgen195 events195 script ui195 strings195 enchants195 effects195 biomes195 audio195 projectiles195; do echo "== modules/$$m"; bash modules/$$m/test.sh || exit 1; done
	@echo "== modules/netsync195"; $(MAKE) -s -C modules/netsync195 test
	@echo "== server"; $(MAKE) -s -C server test

ps4:
	./build-ps4.sh

clean:
	rm -rf build
.PHONY: all test ps4 clean modules-test
