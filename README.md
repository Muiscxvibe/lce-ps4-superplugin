# LCE PS4 superplugin

GoldHEN session plugin for Minecraft PlayStation 4 Edition 1.95 (CUSA00265)
that lets the game join the LCE Server without PSN.

The [modloader SDK](modloader-sdk/README.md) covers mod packages, the public
headers, Lua bindings and the retail 1.95 backends.

## Offline modloader preview

The preview targets **CUSA00265, app version 1.95**, with GoldHEN. It supports
local worlds, experimental custom furnace fuel, and the local debug and player
NBT menus. LCE Server sign-in, LAN discovery, catalog downloads and plugin
auto-updates are disabled in this profile. Existing downloaded content can still
load locally.

Close Minecraft before replacing the active plugin. Save a copy of that PRX
outside GoldHEN's active plugin paths, then replace it at the path already listed
for CUSA00265 in `/data/GoldHEN/plugins.ini`. Keep one active entry for the
game. Restart Minecraft and test in a disposable local world. To roll back,
close the game and restore the saved PRX.

- Direct furnace placement supports declarative and Lua fuel. Quick Move sends
  custom fuel to inventory. Hopper insertion remains experimental.
- Enter the local debug menu with **L3 + R3**. In the NBT tab, open **Me**.
  Square applies the staged changes. Select **Undo last apply (local)** in the
  R1 action menu to reverse the last apply within the same editing session.
- Huge local worlds are labeled **Custom** on the world-size selector. The
  option targets 2,048 × 2,048 chunks. Terrain generation and save/reload were
  verified on a console, with full-size travel still untested.

The SDK host suites run with `make -C modloader-sdk test`. The build script
for the full plugin uses the local r72 integration workspace, prebuilt
components and OpenOrbis. Apply
[`modloader-r72.patch`](plugin/patches/modloader-r72.patch) from the workspace
root and overlay the plugin and transport sources in their respective build
directories. Build the SDK with `LCE_EXPERIMENTAL_FUEL=1`. Pass its combined
object as `LCE_SDK_OBJECT` to the plugin build with
`LCE_RELEASE=1 LCE_OFFLINE_PREVIEW=1`. The preview build does not require a
server signing key.

- `transport/`: session transport, account sign-in, join hooks and the signed
  anticheat client report (`client_report.c`).
- `plugin/`: debug-action forwarding hooks, crash logger and the build script.
- `official-builds.json`: the list of official plugin builds. **The LCE
  Server's anticheat reads this file from GitHub** to decide which plugin
  builds are accepted (others are banned, listed outdated ones get an
  update notice).
- Releases: each official build is a GitHub release with the `.prx` attached.

The anticheat HMAC key (`client_report_key.h`) is generated locally and is
not part of this repository.

## Publishing a build
`scripts/publish_plugin.sh` is for the official server-connected channel.
`scripts/publish_plugin.sh <built super195.prx> <build id> <game text sha256>`
creates the release, uploads the PRX and adds the build to
`official-builds.json` (previous builds become "outdated").
