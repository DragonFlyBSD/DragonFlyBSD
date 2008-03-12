/*
 * Copyright (c) 2004 Scott Ullrich <GeekGod@GeekGod.com> 
 * Portions Copyright (c) 2004 Chris Pressey <cpressey@catseye.mine.nu>
 *
 * Copyright (c) 2004 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Scott Ullrich and Chris Pressey (see above for e-mail addresses).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS, CONTRIBUTORS OR VOICES IN THE AUTHOR'S HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* 
 * common.c - common functions for dfuibe_lua
 * $Id: common.c,v 1.56 2005/04/04 13:56:37 den Exp $
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "lua50/lua.h"
#include "lua50/lauxlib.h"
#include "lua50/lualib.h"

#include "lua_dfui.h"

/*----------------------------- utilities ---------------------------*/

/*
 * Given the names of two global variables, the first a regular
 * 'Class' table, and the second a metatable which will be attached
 * to all object 'instances':
 * - add an __index property to the metatable that redirects
 *   all accesses on the instance to the class table;
 * - add a __metatable propery to the metatable, to hide it.
 */
void
lua_set_instance_handler(lua_State *L,
			 const char *table_name, const char *metatable_name)
{
        int metatable_idx, methods_idx;

	lua_pushstring(L, table_name);		/* name of our 'class' table */
	lua_gettable(L, LUA_GLOBALSINDEX);	/* get it from globals */
        methods_idx = lua_gettop(L);            /* Find its position on the stack */

	lua_pushstring(L, metatable_name);	/* name of our metatable */
	lua_gettable(L, LUA_GLOBALSINDEX);	/* get it from globals */
        metatable_idx = lua_gettop(L);          /* Find its position on the stack */

	/*
	 * Add __index event to metatable (metatable.__index = methods).
	 * This lets the Lua script refer to the methods by indexing
	 * the instance variable like so: x:y(z).
	 */
        lua_pushliteral(L, "__index");
        lua_pushvalue(L, methods_idx);
        lua_settable(L, metatable_idx);

        lua_pushliteral(L, "__metatable");      /* hide metatable */
        lua_pushvalue(L, methods_idx);
        lua_settable(L, metatable_idx);         /* metatable.__metatable = methods */

	lua_pop(L, 2);
}

/*
 * Retrieve a string from a Lua table.
 */
const char *
lua_access_table_string(lua_State *L, int table_idx, const char *key)
{
	const char *s;

	lua_pushlstring(L, key, strlen(key));
	lua_gettable(L, table_idx);
	if (lua_isstring(L, lua_gettop(L))) {
		s = luaL_checkstring(L, lua_gettop(L));
	} else {
		s = "";
	}
	lua_pop(L, 1);

	return(s);
}

/*
 * This function is adapted from liolib.c: push a FILE * onto the
 * Lua stack as a file object that Lua's file module understands.
 */
void
lua_pushfileptr(lua_State *L, FILE *f)
{
	FILE **pf;

	pf = (FILE **)lua_newuserdata(L, sizeof(FILE *));
	*pf = f;  
	luaL_getmetatable(L, "FILE*");
	lua_setmetatable(L, -2);
}

void
lua_show_debug(lua_State *L)
{
	lua_Debug X;

	lua_getstack(L, 0, &X);
	lua_getinfo(L, "nluS", &X);
	fprintf(stderr, "--+-- BEGIN Lua Debug Info --+--\n");
	fprintf(stderr, "source:      %s\n", X.short_src);
	fprintf(stderr, "linedefined: %d\n", X.linedefined);
	fprintf(stderr, "what:        %s\n", X.what);
	fprintf(stderr, "name:        %s\n", X.name);
	fprintf(stderr, "namewhat:    %s\n", X.namewhat);
	fprintf(stderr, "nups:        %d\n", X.nups);
	fprintf(stderr, "--+--  END  Lua Debug Info --+--\n");
}

/*------------------------ module entry point ----------------------*/

LUA_API int
luaopen_ldfui(lua_State *L)
{
	int container_idx;

	/*
	 * Push a new table, which will contain all of our sub-packages,
	 * and right before it, push the name that we will give it.
	 */
	lua_pushstring(L, "DFUI");
	lua_newtable(L);

	/*
	 * Find out where the table is, so we can refer to it easily.
	 */
        container_idx = lua_gettop(L);

	/*
	 * Initialize each sub-package and push it on the stack, then
	 * assign it to a slot in our master table.
	 */

        lua_pushliteral(L, "Connection");
	lua_dfui_register(L);
        lua_settable(L, container_idx);

        lua_pushliteral(L, "Progress");
	lua_dfui_progress_register(L);
        lua_settable(L, container_idx);

	/*
	 * Now that all that is done, put our master table into the
	 * globals table, using the name we gave it at the beginning.
	 */
	lua_settable(L, LUA_GLOBALSINDEX);

	/*
	 * Get our master table out of the globals and push it onto
	 * the stack, so we can return it to whatever script require()d us.
	 */
	lua_pushstring(L, "DFUI");
	lua_gettable(L, LUA_GLOBALSINDEX);

	return(1);
}
