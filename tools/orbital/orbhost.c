/* orbhost.c -- float Lua + source/abs_orbital.c: the math.frexp service of
 * abs_lua.c, for a harness script. ./orbhost <root> <apk assets> script.lua */
#include <dirent.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "abs.h"
#include "lstate.h"
#include "lobject.h"

static const char *g_root, *g_apk;
const char *dcr_game_root(void) { return g_root; }
int g_quiet;
void debugPrintf(const char *fmt, ...) {
  if (g_quiet) return;
  va_list ap; va_start(ap, fmt); printf("    [c] "); vprintf(fmt, ap); va_end(ap);
}
void *abs_asset_read(const char *name, size_t *len) {
  char p[800]; snprintf(p, sizeof p, "%s/%s", g_apk, name);
  FILE *f = fopen(p, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  void *b = malloc((size_t)n + 1); fread(b, 1, (size_t)n, f); fclose(f); *len = (size_t)n; return b;
}
void abs_assets_each(const char *prefix, void (*cb)(const char *name, void *ud), void *ud) {
  char dir[800]; snprintf(dir, sizeof dir, "%s/%s", g_apk, prefix);
  DIR *d = opendir(dir); struct dirent *de;
  while (d && (de = readdir(d))) { char n[800]; snprintf(n, sizeof n, "%s%s", prefix, de->d_name); cb(n, ud); }
  if (d) closedir(d);
}
static int push_string(lua_State *L, const char *s) { lua_pushstring(L, s); return 1; }
static lua_CFunction o_frexp;
static int h_frexp(lua_State *L) {
  if (lua_gettop(L) != 2) return o_frexp(L);
  const char *op = lua_tostring(L, 1), *arg = lua_tostring(L, 2);
  if (!op || !arg || strncmp(op, "orb:", 4)) return o_frexp(L);
  if (!strcmp(op, "orb:present")) return push_string(L, abs_orbital_present() ? "1" : "");
  if (!strcmp(op, "orb:load")) {
    size_t n; uint8_t *plain = abs_orbital_lua_plain(arg, &n);
    if (!plain) return push_string(L, "not in orbital/data");
    char name[200]; snprintf(name, sizeof name, "@orbital/%s", arg);
    luaL_loadbuffer(L, (const char *)plain, n, name); free(plain); return 1;
  }
  if (!strcmp(op, "orb:sheets") || !strcmp(op, "orb:gamesheets") || !strcmp(op, "orb:parts") || !strcmp(op, "orb:quoted") || !strcmp(op, "orb:size") || !strcmp(op, "orb:fullsheets")) {
    char *list = !strcmp(op, "orb:sheets") ? abs_orbital_sheets_for(arg) : !strcmp(op, "orb:gamesheets") ? abs_orbital_game_sheets_for(arg)
               : !strcmp(op, "orb:parts") ? abs_orbital_parts(arg) : !strcmp(op, "orb:size") ? abs_orbital_sprite_size(arg) : !strcmp(op, "orb:fullsheets") ? abs_orbital_full_sheets_for(arg) : abs_orbital_quoted(arg);
    int r = push_string(L, list ? list : ""); free(list); return r;
  }
  if (!strcmp(op, "orb:compos")) {
    char n[16]; snprintf(n, sizeof n, "%d", abs_orbital_group_composites(arg)); return push_string(L, n);
  }
  if (!strcmp(op, "orb:trimmed")) return push_string(L, abs_orbital_trimmed() ? "1" : "");
  if (!strcmp(op, "orb:prune")) {
    /* files are removed only from a scratch copy of the mod: ORB_PRUNE=1 and
     * a .scratch_copy file beside its orbital/ -- never the mod as downloaded */
    char p[800]; snprintf(p, sizeof p, "%s/.scratch_copy", g_root);
    if (!getenv("ORB_PRUNE") || access(p, F_OK) != 0) return push_string(L, "(orbital/data left as it is: not a scratch copy)");
    char *r = abs_orbital_prune(arg, 1); int k = push_string(L, r ? r : ""); free(r); return k;
  }
  if (!strcmp(op, "orb:text")) {
    size_t n; uint8_t *plain = abs_orbital_lua_plain(arg, &n);
    if (!plain || (n && plain[0] == 0x1b)) { free(plain); return push_string(L, ""); }
    char eq[16] = "";
    for (int k = 0; k < 12; k++) { char close[20]; snprintf(close, sizeof close, "]%s]", eq); if (!strstr((const char *)plain, close)) break; strcat(eq, "="); }
    size_t cap = n + 64; char *chunk = malloc(cap); int r = 0;
    int len = snprintf(chunk, cap, "return [%s[\n%s]%s]", eq, (const char *)plain, eq);
    r = luaL_loadbuffer(L, chunk, (size_t)len, "=abs_text") == 0 && lua_pcall(L, 0, 1, 0) == 0;
    free(chunk); free(plain); return r ? 1 : push_string(L, "");
  }
  return push_string(L, "");
}
/* the engine's container: a level as the game would read it */
static int l_asset(lua_State *L) {
  size_t n; void *b = abs_orbital_read(luaL_checkstring(L, 1), &n);
  if (!b) { lua_pushnil(L); return 1; }
  lua_pushlstring(L, b, n); free(b); return 1;
}
/* orb_pc(level): the instruction a Lua function at that stack level is at
 * (the game's scripts carry no line numbers), and where it starts */
static int l_pc(lua_State *L) {
  lua_Debug ar;
  if (!lua_getstack(L, (int)luaL_checkinteger(L, 1), &ar)) return 0;
  CallInfo *ci = L->base_ci + ar.i_ci;
  if (!isLua(ci)) return 0;
  Proto *p = ci_func(ci)->l.p;
  lua_pushinteger(L, (lua_Integer)(ci->savedpc - p->code) - 1);
  lua_pushinteger(L, p->linedefined);
  return 2;
}
int main(int argc, char **argv) {
  g_root = argv[1], g_apk = argv[2];
  g_quiet = getenv("QUIET") != NULL;
  lua_State *L = luaL_newstate(); luaL_openlibs(L);
  lua_getglobal(L, "math"); lua_getfield(L, -1, "frexp"); o_frexp = lua_tocfunction(L, -1); lua_pop(L, 1);
  lua_pushcfunction(L, h_frexp); lua_setfield(L, -2, "frexp"); lua_pop(L, 1);
  lua_register(L, "orb_asset", l_asset);
  lua_register(L, "orb_pc", l_pc);
  lua_newtable(L); for (int i = 3; i < argc; i++) { lua_pushstring(L, argv[i]); lua_rawseti(L, -2, i - 3); } lua_setglobal(L, "arg");
  if (luaL_dofile(L, argv[3])) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); return 1; }
  return 0;
}
