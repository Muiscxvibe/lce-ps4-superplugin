// The legacy staged-zip map importer (the old Emerald launcher flow: built-in
// pinned map index + staged .zip packages imported into savedata) is removed.
// These stand-ins keep the prebuilt Mods menu (Maps section), startup and the
// content notice linking: no packages are ever offered, requests are refused
// with "no importer", and the worker reports idle. The savedata writer
// (map_save_service) is unaffected.
#include <stdint.h>
#include <string.h>

struct lce195_file_ops;
struct mc195_map_job_detail;
typedef struct mc195_map_package {
    char catalog_id[64], filename[96]; uint64_t size; unsigned char sha256[32];
} mc195_map_package;

#define EXPORT extern "C" __attribute__((visibility("hidden")))
EXPORT void mc195_maps_scan(const struct lce195_file_ops *) {}
EXPORT unsigned mc195_maps_index_state(void) { return 2; }           // MC195_MAP_INDEX_UNAVAILABLE
EXPORT unsigned mc195_maps_status(const char *) { return 3; }        // MC195_MAP_UNAVAILABLE
EXPORT uint64_t mc195_maps_revision(void) { return 0; }
EXPORT int mc195_maps_package(const char *, const char *, mc195_map_package *) { return 0; }
EXPORT unsigned mc195_maps_package_count(const char *) { return 0; }
EXPORT int mc195_maps_package_at(const char *, unsigned, mc195_map_package *) { return 0; }
EXPORT unsigned mc195_maps_revalidate_package(const struct lce195_file_ops *, const char *, const char *) { return 3; }
EXPORT unsigned mc195_maps_request(const char *, const char *) { return 3; }   // MC195_MAP_REQUEST_NO_IMPORTER
EXPORT int mc195_map_worker_start(uintptr_t, void (*report)(const char *)) {
    if (report) report("Legacy map importer removed; maps come from the Mods menu Download tab.\n");
    return 1;
}
EXPORT int mc195_map_worker_enqueue(const char *, const char *) { return 0; }
EXPORT unsigned mc195_map_worker_state(void) { return 1; }            // MC195_MAP_JOB_IDLE
EXPORT uint64_t mc195_map_worker_revision(void) { return 0; }
EXPORT int mc195_map_worker_detail(struct mc195_map_job_detail *) { return 0; }
EXPORT unsigned mc195_map_worker_registered_maps(unsigned *count) { if (count) *count = 0; return 2; }
EXPORT const char *mc195_map_step_label(unsigned) { return ""; }
