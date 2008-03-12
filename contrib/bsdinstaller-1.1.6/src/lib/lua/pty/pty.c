/*
 * pty.c - pty bindings for Lua
 * $Id: pty.c,v 1.21 2005/04/04 13:56:37 den Exp $
 *
 * This file was derived in part from DragonFly BSD's
 * src/usr.bin/script/script.c, which contains the following license:
 */
/*
 * Copyright (c) 1980, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <errno.h>
#include <libutil.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lua50/lua.h"
#include "lua50/lauxlib.h"
#include "lua50/lualib.h"

#ifdef WEXITSTATUS
#define WEXIT_TYPE int
#else
#define WEXIT_TYPE union wait
#endif

struct lua_pty {
	FILE		*stream;
	pid_t		 child;
};

#define PTY_TIMEOUT	-1
#define PTY_EOF		-2

LUA_API int luaopen_lpty(lua_State *);

/*** UTILTIES ***/

/*
 * Given the names of two global variables, the first a regular
 * 'Class' table, and the second a metatable which will be attached
 * to all object 'instances':
 * - add an __index property to the metatable that redirects
 *   all accesses on the instance to the class table;
 * - add a __metatable propery to the metatable, to hide it.
 */
static void
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

/*** STACK ACCESS ***/

static struct lua_pty *
lua_check_pty(lua_State *L, int ch_index)
{
	luaL_checktype(L, ch_index, LUA_TUSERDATA);
	lua_getmetatable(L, ch_index);
	lua_pushliteral(L, "PtyMeta");
	lua_rawget(L, LUA_GLOBALSINDEX);
	if (!lua_rawequal(L, -1, -2))
		luaL_typerror(L, ch_index, "Pty");
	lua_pop(L, 2);
	return((struct lua_pty *)lua_unboxpointer(L, ch_index));
}

static struct lua_pty *
lua_push_pty(lua_State *L, struct lua_pty *x)
{
	lua_boxpointer(L, x);
	lua_pushliteral(L, "PtyMeta");
	lua_gettable(L, LUA_GLOBALSINDEX);
	lua_setmetatable(L, -2);
	return(x);
}

/*** CONSTRUCTOR/DESTRUCTOR ***/

static int 
lua_pty_open(lua_State *L)
{
	struct lua_pty *pty;
	int master, slave;

	pty = malloc(sizeof(struct lua_pty));
	if (pty == NULL) {
		lua_pushnil(L);
		lua_pushnumber(L, ENOMEM);
		return(2);
	}
	pty->stream = NULL;
	if (openpty(&master, &slave, NULL, NULL, NULL) == -1) {
		lua_pushnil(L);
		lua_pushnumber(L, errno);
		return(2);
	}

	pty->child = fork();
	if (pty->child < 0) {
		lua_pushnil(L);
		lua_pushnumber(L, errno);
		return(2);
	}
	if (pty->child == 0) {
		const char *shell = "/bin/sh";

		close(master);
		login_tty(slave);
		execl(shell, shell, "-c", luaL_checkstring(L, 1), NULL);
		/* if we made it here, an error occurred! */
	}
	close(slave);

	/*
	 * Convert the file descriptor into a stream, or die trying.
	 */
	if ((pty->stream = fdopen(master, "r+")) == NULL) {
	        WEXIT_TYPE status;

		kill(pty->child, SIGTERM);
		if (waitpid(pty->child, (int *)&status, 0) != pty->child) {
			lua_pushnil(L);
			lua_pushnumber(L, errno);
			return(2);
		}
		lua_pushnil(L);
		lua_pushnumber(L, errno);
		return(2);
	}

	lua_push_pty(L, pty);
	return(1);
}

/******* METHODS *******/

static int
lua_pty_readline(lua_State *L)
{
	struct lua_pty *pty;
	long msec = 0;
	int n, len;
	char line[4096];
	fd_set rfd;
	struct timeval tv;
	struct timeval *tvp = NULL;

	pty = lua_check_pty(L, 1);
	if (lua_isnumber(L, 2)) {
		msec = lua_tonumber(L, 2);
		tvp = &tv;
	}
	lua_pop(L, 2);

	FD_ZERO(&rfd);
	FD_SET(fileno(pty->stream), &rfd);
	if (tvp != NULL) {
		tv.tv_sec = msec / 1000;
		tv.tv_usec = (msec % 1000) * 1000;
	}
	n = select(fileno(pty->stream) + 1, &rfd, 0, 0, tvp);
	if (n < 0) {
		lua_pushnil(L);
		lua_pushnumber(L, errno);
		return(2);
	} else if (n > 0 && FD_ISSET(fileno(pty->stream), &rfd)) {
		if (fgets(line, sizeof(line) - 1, pty->stream) == NULL) {
			lua_pushnil(L);
			if (feof(pty->stream))
				lua_pushnumber(L, PTY_EOF);
			else
				lua_pushnumber(L, errno);			
			return(2);
		} else {
			len = strlen(line);
			while (len > 0 && (line[len - 1] == '\n' ||
					   line[len - 1] == '\r')) {
				line[--len] = '\0';
			}
			lua_pushstring(L, line);
			return(1);
		}
	} else {
		lua_pushnil(L);
		lua_pushnumber(L, PTY_TIMEOUT);
		return(2);
	}
}

static int
lua_pty_write(lua_State *L)
{
	struct lua_pty *pty;
	const char *string;

	pty = lua_check_pty(L, 1);
	string = luaL_checkstring(L, 2);
	lua_pop(L, 2);

	fwrite(string, 1, strlen(string), pty->stream);
	return(0);
}

static int
lua_pty_flush(lua_State *L)
{
	struct lua_pty *pty;
	int result;

	pty = lua_check_pty(L, 1);

	result = fflush(pty->stream);
	lua_pushnumber(L, result);
	return(1);
}

static int
lua_pty_close(lua_State *L)
{
	struct lua_pty *pty;
	WEXIT_TYPE status;
	int e = 0;

	pty = lua_check_pty(L, 1);

	if (pty->stream == NULL) {
		/*
		 * It's already been closed.
		 * Don't try to close it again.
		 */
		lua_pushnumber(L, -1);
		return(1);
	}

	fclose(pty->stream);
	pty->stream = NULL;

	if (waitpid(pty->child, (int *)&status, 0) != pty->child) {
		lua_pushnil(L);
		lua_pushnumber(L, errno);
		return(2);
	}

	if (WIFEXITED(status)) {
		e = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		e = WTERMSIG(status);
	} else {
		/* Only happens when system is out of file descriptors */
		e = 1;
	}

	lua_pushnumber(L, e);
	return(1);
}

static int
lua_pty_signal(lua_State *L)
{
	struct lua_pty *pty;
	int signo, result;

	pty = lua_check_pty(L, 1);
	signo = luaL_checkint(L, 2);
	result = kill(pty->child, signo);
	lua_pushnumber(L, result);

	return(1);
}

/**** Binding Tables ****/

const luaL_reg pty_methods[] = {
	{"open",	lua_pty_open },
	{"readline",	lua_pty_readline },
	{"write",	lua_pty_write },
	{"flush",	lua_pty_flush },
	{"close",	lua_pty_close },
	{"signal",	lua_pty_signal },
	{0, 0}
};

const luaL_reg pty_meta_methods[] = {
	{"__gc",	lua_pty_close },
	{0, 0}
};

/*** REGISTER ***/

LUA_API int
luaopen_lpty(lua_State *L)
{
	int methods_idx;

	luaL_openlib(L, "Pty", pty_methods, 0);		    /* fill methods table */
        luaL_openlib(L, "PtyMeta", pty_meta_methods,  0);   /* fill metatable */
	lua_pop(L, 1);

	lua_set_instance_handler(L, "Pty", "PtyMeta");

	/*
	 * Add some symbolic constants.
	 */
        methods_idx = lua_gettop(L);

        lua_pushliteral(L, "TIMEOUT");
        lua_pushnumber(L, PTY_TIMEOUT);
        lua_settable(L, methods_idx);

        lua_pushliteral(L, "EOF");
        lua_pushnumber(L, PTY_EOF);
        lua_settable(L, methods_idx);

        lua_pushliteral(L, "SIGTERM");
        lua_pushnumber(L, SIGTERM);
        lua_settable(L, methods_idx);

	return(1);
}
