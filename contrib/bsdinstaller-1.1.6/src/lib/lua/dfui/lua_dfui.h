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
 * lua_dfui.h
 * $Id: lua_dfui.h,v 1.69 2005/02/22 07:16:48 cpressey Exp $
 */

#ifndef _LUA_DFUI_H_
#define _LUA_DFUI_H_

/*
 * Macro to define a function that checks that the Lua
 * data object at `index' on Lua's stack is a userdata object
 * of Lua type `sname' and C type `type'.
 * If not, the function throws a Lua error, but if so,
 * it returns the object.
 */
#define LUA_CHECK_FUNCTION(name, sname, type)				\
type	lua_check_##name(lua_State *, int);				\
type									\
lua_check_##name(lua_State *L, int ch_index)				\
{									\
	luaL_checktype(L, ch_index, LUA_TUSERDATA);			\
	lua_getmetatable(L, ch_index);					\
	lua_pushliteral(L, sname "Meta");				\
	lua_rawget(L, LUA_GLOBALSINDEX);				\
	if (!lua_rawequal(L, -1, -2))					\
		luaL_typerror(L, ch_index, sname);			\
	lua_pop(L, 2);							\
	return((type)lua_unboxpointer(L, ch_index));			\
}

/*
 * Macro to definate a function which pushes a
 * Lua `sname' object onto Lua's stack.
 */
#define LUA_PUSH_FUNCTION(name, sname, type)				\
type lua_push_##name(lua_State *, type);				\
type									\
lua_push_##name(lua_State *L, type x)					\
{									\
	lua_boxpointer(L, x);						\
	lua_pushliteral(L, sname "Meta");				\
	lua_gettable(L, LUA_GLOBALSINDEX);				\
	lua_setmetatable(L, -2);					\
	return(x);							\
}

struct dfui_connection;

void	 lua_set_instance_handler(lua_State *L,
	    const char *table_name, const char *metatable_name);
const char *lua_access_table_string(lua_State *, int, const char *);
void	 lua_show_debug(lua_State *);
void	 lua_pushfileptr(lua_State *, FILE *);

LUA_API int	lua_dfui_register(lua_State *);
LUA_API int	lua_dfui_progress_register(lua_State *);

LUA_API int	luaopen_ldfui(lua_State *);

#endif	/* !_DFUIBE_LUA_H_ */
