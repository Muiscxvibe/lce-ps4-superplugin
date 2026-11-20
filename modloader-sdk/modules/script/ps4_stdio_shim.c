/* Inert stdio for the PS4 Lua build (see ps4_stdio_shim.h). No file is ever opened. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
FILE *lce_lua_fopen(const char *p, const char *m) { (void)p; (void)m; return NULL; }
FILE *lce_lua_freopen(const char *p, const char *m, FILE *f) { (void)p; (void)m; (void)f; return NULL; }
int lce_lua_fclose(FILE *f) { (void)f; return 0; }
size_t lce_lua_fread(void *b, size_t s, size_t n, FILE *f) { (void)b; (void)s; (void)n; (void)f; return 0; }
size_t lce_lua_fwrite(const void *b, size_t s, size_t n, FILE *f) { (void)b; (void)s; (void)f; return n; }
int lce_lua_getc(FILE *f) { (void)f; return EOF; }
int lce_lua_feof(FILE *f) { (void)f; return 1; }
int lce_lua_ferror(FILE *f) { (void)f; return 1; }
int lce_lua_fflush(FILE *f) { (void)f; return 0; }
int lce_lua_fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
char *lce_lua_strerror(int e) { static char msg[] = "no files in the LCE script sandbox"; (void)e; return msg; }
