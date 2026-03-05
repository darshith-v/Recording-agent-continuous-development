#ifndef _LUAIF_H_
#define _LUAIF_H_

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern lua_State * luaif_create_extra_lState(const char *preload) ;
extern void luaif_release_extra_lState(lua_State *L) ;
extern void luaif_test(lua_State *L) ;

#endif
