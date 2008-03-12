/*
 * Copyright (c) 2004 Chris Pressey <cpressey@catseye.mine.nu>
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
 * progress.c - libdfui Progress Bar bindings for Lua
 * $Id: progress.c,v 1.16 2005/04/04 13:56:37 den Exp $
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dfui/dfui.h"

#include "lua50/lua.h"
#include "lua50/lauxlib.h"
#include "lua50/lualib.h"

#include "lua_dfui.h"

/*** Prototypes ***/

struct dfui_connection	*lua_check_dfui_connection(lua_State *, int);
struct dfui_connection	*lua_push_dfui_connection(lua_State *, struct dfui_connection *);

/*** Structures ***/

struct lua_dfui_progress {
	struct dfui_connection	*c;
	struct dfui_progress	*pr;
};

LUA_CHECK_FUNCTION(dfui_progress, "DFUIProgress", struct lua_dfui_progress *)
LUA_PUSH_FUNCTION(dfui_progress, "DFUIProgress", struct lua_dfui_progress *)

/*** CONSTRUCTOR & DESTRUCTOR ***/

static int
lua_dfui_progress_new(lua_State *L)
{
	const char *name, *short_desc, *long_desc;
	struct lua_dfui_progress *ldp;
	int amount;

	if ((ldp = malloc(sizeof(struct lua_dfui_progress))) == NULL) {
		lua_pushnil(L);
		lua_pushnumber(L, ENOMEM);
		return(2);
	}

	ldp->c = lua_check_dfui_connection(L, 1);
	name = luaL_checkstring(L, 2);
	short_desc = luaL_checkstring(L, 3);
	long_desc = luaL_checkstring(L, 4);
	amount = lua_tonumber(L, 5);

	ldp->pr = dfui_progress_new(dfui_info_new(name, short_desc, long_desc), amount);

	lua_push_dfui_progress(L, ldp);
	return(1);
}

static int
lua_dfui_progress_destroy(lua_State *L)
{
	struct lua_dfui_progress *ldp;

	ldp = (struct lua_dfui_progress *)lua_unboxpointer(L, 1);
	if (ldp != NULL) {
		/*
		 * We didn't allocate the connection,
		 * so we don't free it here either.
		 */
		dfui_progress_free(ldp->pr);
		free(ldp);
	}
	return(0);
}

/*** BOUND METHODS ***/

static int
lua_dfui_progress_begin(lua_State *L)
{
	struct lua_dfui_progress *ldp;

	ldp = lua_check_dfui_progress(L, 1);
	dfui_be_progress_begin(ldp->c, ldp->pr);

	return(0);
}

static int
lua_dfui_progress_end(lua_State *L)
{
	struct lua_dfui_progress *ldp;

	ldp = lua_check_dfui_progress(L, 1);
	dfui_be_progress_end(ldp->c);

	return(0);
}

static int
lua_dfui_progress_update(lua_State *L)
{
	struct lua_dfui_progress *ldp;
	int cancelled;

	ldp = lua_check_dfui_progress(L, 1);
	dfui_be_progress_update(ldp->c, ldp->pr, &cancelled);

	lua_pushboolean(L, !cancelled);
	return(1);
}

static int
lua_dfui_progress_set_amount(lua_State *L)
{
	struct lua_dfui_progress *ldp;
	int amount;

	ldp = lua_check_dfui_progress(L, 1);
	amount = lua_tonumber(L, 2);
	dfui_progress_set_amount(ldp->pr, amount);

	return(0);
}

static int
lua_dfui_progress_set_short_desc(lua_State *L)
{
	struct lua_dfui_progress *ldp;
	const char *short_desc;

	ldp = lua_check_dfui_progress(L, 1);
	short_desc = luaL_checkstring(L, 2);
	dfui_info_set_short_desc(dfui_progress_get_info(ldp->pr), short_desc);

	return(0);
}

/**** Binding Tables ****/

const luaL_reg dfui_progress_methods[] = {
	{"new",			lua_dfui_progress_new },
	{"start",		lua_dfui_progress_begin },
	{"stop",		lua_dfui_progress_end },
	{"update",		lua_dfui_progress_update },
	{"set_amount",		lua_dfui_progress_set_amount },
	{"set_short_desc",	lua_dfui_progress_set_short_desc },
	{0, 0}
};

const luaL_reg dfui_progress_meta_methods[] = {
	{"__gc",	lua_dfui_progress_destroy },
	{0, 0}
};


/*** REGISTRATION ***/

LUA_API int
lua_dfui_progress_register(lua_State *L)
{
	luaL_openlib(L, "DFUIProgress",
			dfui_progress_methods, 0);		/* fill methods table */
        luaL_openlib(L, "DFUIProgressMeta",
			dfui_progress_meta_methods,  0);	/* fill metatable */
	lua_pop(L, 1);

	lua_set_instance_handler(L, "DFUIProgress", "DFUIProgressMeta");

	return(1);
}
