# Host tests for the LCE|ModIds server core (g++ + ASan/UBSan).
#   make test    build and run
#   make patch   regenerate server-sync.patch from base/ + patched/ + new/
CXX ?= g++
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -Werror $(SAN)
B = build/host
NS = ../modules/netsync195
INC = -Inew/Minecraft.World -Itests

all: test

$(B)/test_modids_core: tests/test_modids_core.cpp tests/testkit.h new/Minecraft.World/LceModIdCore.h new/Minecraft.World/LceModIdWire.h $(NS)/netsync_client.cpp $(NS)/netsync_client.h
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) $(INC) -I$(NS) tests/test_modids_core.cpp $(NS)/netsync_client.cpp -o $@

$(B)/test_modchannel: tests/test_modchannel.cpp new/Minecraft.World/LceModChannelCore.h new/Minecraft.Client/LceModChannelRelay.h tests/fakeinc/Minecraft.World/CustomPayloadPacket.h
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -Itests/fakeinc/Minecraft.World tests/test_modchannel.cpp -o $@

test: $(B)/test_modids_core $(B)/test_modchannel
	cmp new/Minecraft.World/LceModIdWire.h $(NS)/lce_modids_wire.h
	@if [ -d base ]; then cd base && sha256sum -c --quiet ../patch-base.sha256; else echo "SKIP server patch base checksum: source snapshot is absent"; fi
	$(B)/test_modids_core
	$(B)/test_modchannel

patch:
	./make-patch.sh
	./make-modch-patch.sh

.PHONY: all test patch
