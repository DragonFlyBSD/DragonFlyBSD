/*
 * $Id: lua_gettext.c,v 1.9 2005/04/04 13:56:37 den Exp $
 */

#include <lua50/lua.h>
#include <lua50/lualib.h>
#include <lua50/lauxlib.h>

#include "libintl.h"

/*** Prototypes ***/

LUA_API int luaopen_lgettext(lua_State *);

/*** Globals ***/

const char *package = "";
const char *locale_dir = "";

/*** Methods ***/

static int
lua_gettext_init(lua_State *L __unused)
{
	setlocale(LC_ALL, "");
	bindtextdomain(package, locale_dir);
	textdomain(package);

	return(0);
}

static int
lua_gettext_set_package(lua_State *L)
{
	package = luaL_checkstring(L, 1);

	return(0);
}

static int
lua_gettext_set_locale_dir(lua_State *L)
{
	locale_dir = luaL_checkstring(L, 1);

	return(0);
}

static int
lua_gettext_translate(lua_State *L)
{
	lua_pushstring(L, gettext(luaL_checkstring(L, 1)));
	lua_pushstring(L, luaL_checkstring(L, 1));

	return(1);
}

/**** Binding Tables ****/

const luaL_reg gettext_methods[] = {
	{"init",		lua_gettext_init },
	{"set_package",		lua_gettext_set_package },
	{"set_locale_dir",	lua_gettext_set_locale_dir },
	{"translate",		lua_gettext_translate },

	{0, 0}
};

/*** REGISTER ***/

LUA_API int
luaopen_lgettext(lua_State *L)
{
	luaL_openlib(L, "GetText", gettext_methods, 0);

	return(1);
}
