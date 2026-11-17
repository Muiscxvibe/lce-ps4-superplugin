#pragma once
#include <stdint.h>
namespace lce_native_identity {
// The profile manager and executable must remain valid during the call.
// `uid` must be a writable native PlayerUID buffer of at least 28 bytes.
bool bind(uintptr_t base);
bool init_empty(void *uid);
// Obtain this console profile's offline UID without requiring a PSN login.
// This is a native local identity, not an authenticated service account.
bool get_local(int user_index, void *uid);
}
