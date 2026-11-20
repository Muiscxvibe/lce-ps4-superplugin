/* Forced include for the PS4 build of Lua (build-ps4.sh: -include). The sandbox
 * never opens files and routes print() to the mod log, so lauxlib/lbaselib's
 * stdio references (luaL_loadfilex, lua_writestring*) are redirected to inert
 * stubs. That keeps the static libc.a stdio, and its raw syscall imports
 * (_open/_read/_write/...), out of the shipping plugin. Host builds do not use it. */
#ifndef LCE_PS4_STDIO_SHIM_H
#define LCE_PS4_STDIO_SHIM_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE *lce_lua_fopen(const char *, const char *);
FILE *lce_lua_freopen(const char *, const char *, FILE *);
int lce_lua_fclose(FILE *);
size_t lce_lua_fread(void *, size_t, size_t, FILE *);
size_t lce_lua_fwrite(const void *, size_t, size_t, FILE *);
int lce_lua_getc(FILE *);
int lce_lua_feof(FILE *);
int lce_lua_ferror(FILE *);
int lce_lua_fflush(FILE *);
int lce_lua_fprintf(FILE *, const char *, ...);
char *lce_lua_strerror(int);
#ifdef __cplusplus
}
#endif
#undef stdin
#undef stdout
#undef stderr
#undef getc
#define stdin ((FILE *)0)
#define stdout ((FILE *)0)
#define stderr ((FILE *)0)
#define fopen lce_lua_fopen
#define freopen lce_lua_freopen
#define fclose lce_lua_fclose
#define fread lce_lua_fread
#define fwrite lce_lua_fwrite
#define getc lce_lua_getc
#define feof lce_lua_feof
#define ferror lce_lua_ferror
#define fflush lce_lua_fflush
#define fprintf lce_lua_fprintf
#define strerror lce_lua_strerror
#endif
