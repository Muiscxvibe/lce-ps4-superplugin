#pragma once
#include "external_players.hpp"
namespace lce_external_players {
// The caller must provide the game's socket retirement operation, after the
// connection and transport have drained. No session is published here.
bool bind_native(uintptr_t game_base);
Ops native_ops(bool (*retire_socket)(void *,void *));
// Uses the local console profile and an initialized empty host UID. The caller
// supplies the native SQR manager and a server-assigned nonzero player ID.
// Successful preparation does not publish the players or start game traffic.
bool prepare_native(Players &, void *manager, int user_index, uint8_t local_id,
                    bool (*retire_socket)(void *,void *));
}
