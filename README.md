# LCE PS4 superplugin

GoldHEN session plugin for Minecraft PlayStation 4 Edition 1.95 (CUSA00265)
that lets the game join the LCE Server without PSN.

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
`scripts/publish_plugin.sh <built super195.prx> <build id> <game text sha256>`
creates the release, uploads the PRX and adds the build to
`official-builds.json` (previous builds become "outdated").
