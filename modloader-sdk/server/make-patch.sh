#!/bin/sh
# Regenerates server-sync.patch (apply with: cd neo-ps4-mob-candidate && patch -p1 < .../server-sync.patch)
set -e
cd "$(dirname "$0")"
out=server-sync.patch
: > $out
for f in Minecraft.Client/PlayerConnection.cpp Minecraft.World/Connection.cpp Minecraft.Server/Windows64/ServerMain.cpp; do
  diff -u --label a/$f --label b/$f base/$f patched/$f >> $out || true
done
for f in Minecraft.World/LceModIdWire.h Minecraft.World/LceModIdCore.h Minecraft.World/LceModIdRegistry.h Minecraft.Client/LceModIdHandshake.h; do
  diff -uN --label /dev/null --label b/$f /dev/null new/$f >> $out || true
done
