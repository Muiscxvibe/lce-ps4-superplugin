/* Builds the LCE|ModIds entry list from the SDK loader's committed state. */
#pragma once
#include "lce_modids_wire.h"
#include "../../loader/ledger.h"
#include "../../loader/backend.h"

namespace lce_netsync {

/* One entry per ledger entry, in ledger order (so the digest equals the
 * ledger file's sha256 trailer). An entry is flagged live when the committed
 * plan registered it AND `published` says the backend publish succeeded;
 * properties come from the plan descriptor (max stack 0 -> 64, block hardness
 * and resistance in hundredths, saturated). Returns the entry count, or -1 if
 * `cap` is too small or a ledger entry cannot be represented on the wire. */
int entries_from_sdk(const lce_ledger *ledger, const lce_plan *plan, bool published,
                     lce_modids::Entry *out, unsigned cap);

}  // namespace lce_netsync
