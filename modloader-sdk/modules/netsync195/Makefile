# Host tests for netsync195 (g++ + ASan/UBSan). Links the real SDK loader core
# (read-only) so entries_from_sdk and the digest are checked against it.
CXX ?= g++
CC ?= gcc
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -Werror $(SAN) -pthread
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror $(SAN)
B = build/host
SDK = ../..
CORE = $(SDK)/loader/package.cpp $(SDK)/loader/ledger.cpp $(SDK)/loader/loader.cpp $(SDK)/loader/ext.cpp
SRC = netsync_client.cpp netsync_sdk.cpp netsync_plugin.cpp

all: test

$(B)/sha256.o: $(SDK)/loader/sha256.c $(SDK)/loader/sha256.h
	@mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/test_netsync: tests/test_netsync.cpp $(SRC) netsync_client.h netsync_sdk.h lce_modids_wire.h $(B)/sha256.o
	$(CXX) $(CXXFLAGS) -I../../server/new/Minecraft.World tests/test_netsync.cpp $(SRC) $(CORE) $(B)/sha256.o -o $@

test: $(B)/test_netsync
	cmp lce_modids_wire.h ../../server/new/Minecraft.World/LceModIdWire.h
	cd base && sha256sum -c --quiet ../patch-base.sha256
	$(B)/test_netsync

.PHONY: all test
