#!/bin/sh
# Regenerates mod-channels.patch: the mod channel relay (API 1.6 client networking), applied AFTER server-sync.patch:
#   cd neo-ps4-mob-candidate && patch -p1 < .../server-sync.patch && patch -p1 < .../mod-channels.patch
set -e
cd "$(dirname "$0")"
out=mod-channels.patch
: > $out
f=Minecraft.Client/PlayerConnection.cpp
diff -u --label a/$f --label b/$f patched/$f modch/$f >> $out || true
for f in Minecraft.World/LceModChannelCore.h Minecraft.Client/LceModChannelRelay.h; do
  diff -uN --label /dev/null --label b/$f /dev/null new/$f >> $out || true
done
