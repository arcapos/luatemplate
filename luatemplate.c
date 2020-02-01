/*
* Copyright (C) 2011 - 2020 Micro Systems Marc Balmer, CH-5073 Gipf-Oberfrick.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Standalone Lua Templates renderer */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "luatemplate.h"

static char *render_block =
"	local env, t, b = ...\n"
"	while t ~= nil and template[t].blk[b] == nil do\n"
"		t = template[t].extends\n"
"	end\n"
"	if t ~= nil and template[t].blk[b] ~= nil then\n"
"		template[t].blk[b](env)\n"
"	end\n";

static char *render_template =
"	local env, n = ...\n"
"	local tn = n\n"
"\n"
"	while template[n].extends ~= nil do\n"
"		n = template[n].extends\n"
"	end\n"
"	env.print = print\n"
"	env.string = string\n"
"	env.render_block = render_block\n"
"	env.render_template = render_template\n"
"	template[n].main(env, tn)\n";

static int
template_context(lua_State *L)
{
	struct render_context *ctx;
	int setup, print, finish;

	ctx = lua_newuserdata(L, sizeof(struct render_context));
	luaL_setmetatable(L, TEMPLATE_CONTEXT_METATABLE);
	SLIST_INIT(&ctx->ihead);

	/* The container for everything */
	lua_newtable(L);

	/* A place to store templates, initially empty */
	lua_newtable(L);
	lua_setfield(L, -2, "template");

	lua_getglobal(L, "string");
	lua_setfield(L, -2, "string");

	/* Add the required functions to the container */
	if (luaL_loadbuffer(L, render_block, strlen(render_block),
	    "render_block"))
	    	printf("error loading render_block function\n");
	lua_pushvalue(L, -2);
	lua_setupvalue(L, -2, 1);
	lua_setfield(L, -2, "render_block");

	if (luaL_loadbuffer(L, render_template, strlen(render_template),
	    "render_template"))
	    	printf("error loading render_template function\n");
	lua_pushvalue(L, -2);
	lua_setupvalue(L, -2, 1);
	lua_setfield(L, -2, "render_template");

	/* Associate the container with this context */
	lua_setuservalue(L, -2);
	ctx->debug = 0;
	return 1;
}

static int
render_file(lua_State *L)
{
	struct stat sb;
	struct lt_include_head ihead;
	FILE *fp = NULL;
	struct auth *auth;
	time_t mtime;
	char errmsg[BUFSIZ];
	const char *fnam;
	int funcindex;
	struct render_context *ctx;
	int globals;

	ctx = luaL_checkudata(L, 1, TEMPLATE_CONTEXT_METATABLE);
	fnam = luaL_checkstring(L, 2);

	if (stat(fnam, &sb))
		luaL_error(L, "can't stat %s", fnam);

	lua_getuservalue(L, 1);
	if (lua_gettop(L) == 5) {
		lua_pushvalue(L, 4);
		lua_setfield(L, -2, "print");
	} else {
		lua_getglobal(L, "io");;
		lua_getfield(L, -1, "write");
		lua_setfield(L, -3, "print");
		lua_pop(L, 1);
	}

	lua_getfield(L, -1, "template");
	lua_getfield(L, -1, fnam);
	if (!lua_isnil(L, -1)) {	/* check mtime */
		lua_getfield(L, -1, "mtime");
		mtime = (time_t)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		if (mtime != sb.st_mtime) {
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_setfield(L, -2, fnam);
			lua_pushnil(L);
		}
	}

	if (lua_isnil(L, -1)) {
		lua_pop(L, 2);
		if (process_file(L, fnam, &ctx->ihead, ctx->debug)) {
			return luaL_error(L, "processing failed, %s: %s",
			    lua_tostring(L, -1), lua_tostring(L, -2));
		} else {
			lua_getfield(L, -1, "template");
			lua_getfield(L, -1, fnam);
			lua_pushinteger(L, sb.st_mtime);
			lua_setfield(L, -2, "mtime");
			lua_pop(L, 2);
		}
	} else
		lua_pop(L, 2);

	lua_getfield(L, -1, "render_template");
	lua_pushvalue(L, 3);
	lua_pushstring(L, fnam);
	if (lua_pcall(L, 2, 0, 0)) {
		printf("\nrender error, %s\n", lua_tostring(L, -1));
		luaL_error(L,
#if LT_DEBUG
		    lt_errmsg(L));
#else
		    lua_tostring(L, -1));
#endif
		lua_pop(L, 1);	/* error code */
		lua_pushnil(L);
		return 2;
	}

	lua_pop(L, 2);

	lua_pushboolean(L, 1);
	return 1;
}

static int
render_debug(lua_State *L)
{
	struct render_context *ctx;

	ctx = luaL_checkudata(L, 1, TEMPLATE_CONTEXT_METATABLE);
	ctx->debug = lua_toboolean(L, 2);
	return 0;
}

static int
render_clear(lua_State *L)
{
	struct render_context *ctx;

	ctx = luaL_checkudata(L, 1, TEMPLATE_CONTEXT_METATABLE);
	lua_pushnil(L);
	lua_setuservalue(L, -2);
	return 0;
}

int
luaopen_template(lua_State *L)
{
	struct luaL_Reg luatemplate[] = {
		{ "context",	template_context },
		{ NULL, NULL }
	};
	struct luaL_Reg render_methods[] = {
		{ "renderFile",		render_file },
		{ "debug",		render_debug },
		{ NULL, NULL }
	};
	luaL_newlib(L, luatemplate);

	if (luaL_newmetatable(L, TEMPLATE_CONTEXT_METATABLE)) {
		luaL_setfuncs(L, render_methods, 0);
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, render_clear);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2016 - 2020 "
	    "micro systems marc balmer");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "Lua Templates");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "template 1.1.0");
	lua_settable(L, -3);

	return 1;
}
