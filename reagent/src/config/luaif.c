#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "civetweb.h"
#include "http.h"

extern int luaopen_LuaXML_lib(lua_State *);
extern int luaopen_LuaJSC_lib(lua_State *);
extern int luaopen_lfs(lua_State *);
extern void xtra_lib_shared_register(struct lua_State *L) ;

lua_State * luaif_create_extra_lState(const char *preload)
{
    lua_State *L = luaL_newstate(); ;
    
    if (L != NULL) {
    	luaL_openlibs(L);
    	if ((preload) && (preload[0] != '\0')) {
    		luaL_dofile(L, preload) ;
    	}

		luaopen_lfs(L);

		luaopen_LuaXML_lib(L);
		// lua_pushvalue(L, -1); to copy value
		lua_setglobal(L, "xml");

		luaopen_LuaJSC_lib(L);
		lua_setglobal(L, "jsc");

		xtra_lib_shared_register(L) ;
    }
    else {
    	fprintf(stderr, "Can't make a new state\n") ;
    }

    return L ;
}

void luaif_release_extra_lState(lua_State *L)
{
	if (L) {
		lua_close(L) ;
	}
}

void luaif_test(lua_State *L)
{
#if 0
	const char *c_function = "local a=5 local b=10 local c=a+b return c";
	int r;
	char buff[1024];
	int error;

	fprintf(stderr, "Testing LUA from C :\n") ; fflush(stderr) ;

	luaL_dostring(L,c_function);
	r = luaL_checkinteger(L, -1);
	fprintf(stderr, "\n5+10 = %d\n",r);

	while (fgets(buff, sizeof(buff), stdin) != NULL) {
		if (buff[0] == '.') {
			break ;
		}
		error = luaL_loadbuffer(L, buff, strlen(buff), "line") ||
			lua_pcall(L, 0, 0, 0);
		if (error) {
			fprintf(stderr, "%s", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}

	fprintf(stderr, "Done.\n\n") ;
#endif
}
