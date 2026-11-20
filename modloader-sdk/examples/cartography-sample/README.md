# Cartography Sample

This offline CUSA00265 1.95 preview package adds two waypoint markers and an area outline to the F3 server overlay. Build it with `python3 tools/lce_modpack.py build examples/cartography-sample out` from the SDK directory. The SDK and plugin need `LCE_WAYPOINT_PACKAGES=1`. The plugin also needs `LCE_F3_WAYPOINTS=1 LCE_OFFLINE_PREVIEW=1`.

Add up to 16 entries to the `waypoints` array in your mod manifest. Each point has an integer `[x, y, z]` position. An area adds an opposite `[x, z]` corner. Use the native level index from 0 through 2 for `layer`, a label of 1 to 24 printable ASCII characters, and an optional RGB color such as `#55ccff`. Coordinates may range from −30,000,000 through 30,000,000 blocks.

Enable the package and enter the intended local world. Turn on the **F3 server overlay** for your player. Open **Lists → Show package waypoints** in the debug menu and return to gameplay within 30 seconds. Each local player selects their own overlay. Closing F3, leaving the world, or changing dimension clears the selection.
