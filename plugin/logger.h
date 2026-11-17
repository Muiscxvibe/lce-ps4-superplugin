#ifndef MC195_MODS_CRASH_LOGGER_H
#define MC195_MODS_CRASH_LOGGER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void mc195_crashlog_start(uintptr_t game_base);
void mc195_crashlog_rearm(void);
void mc195_crashlog_stage(unsigned stage, uintptr_t scene, unsigned detail);
#ifdef __cplusplus
}
#endif
#endif
