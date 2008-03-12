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
 * dfui.c - libdfui Bindings for Lua
 * $Id: dfui.c,v 1.64 2005/04/04 13:56:37 den Exp $
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dfui/dfui.h"
#include "dfui/dump.h"
#include "dfui/system.h"

#include "lua50/lua.h"
#include "lua50/lauxlib.h"
#include "lua50/lualib.h"

#include "lua_dfui.h"

LUA_CHECK_FUNCTION(dfui_connection, "DFUIConnection", struct dfui_connection *)
LUA_PUSH_FUNCTION(dfui_connection, "DFUIConnection", struct dfui_connection *)

#define DFUI_OBJ_FORM	1
#define DFUI_OBJ_FIELD	2
#define DFUI_OBJ_ACTION	3

static void
dfui_field_options_from_lua_table(lua_State *L, struct dfui_field *fi)
{
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (lua_isstring(L, -1)) {
			dfui_field_option_add(fi, lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}
}

static void
set_dfui_properties_from_lua_table(lua_State *L, int table_idx,
				   int dfui_obj_type, void *dfui_obj)
{
	const char *key, *value;

	/*
	 * Traverse the table, looking for key->value pairs that we can use
	 * to modify the field.
	 * For each entry, if it is standard (id, name, short_desc, long_desc)
	 * ignore it; if it is anything else, assume it is a property.
	 */
	lua_pushnil(L);
	while (lua_next(L, table_idx) != 0) {
		if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
			key = lua_tostring(L, -2);
			value = lua_tostring(L, -1);

			if (strcmp(key, "id") == 0 ||
			    strcmp(key, "name") == 0 ||
			    strcmp(key, "short_desc") == 0 ||
			    strcmp(key, "long_desc") == 0) {
				/*
				 * Skip it, we've already done it.
				 */
			} else if (strcmp(key, "multiple") == 0 &&
			    dfui_obj_type == DFUI_OBJ_FORM) {
				dfui_form_set_multiple(
				    (struct dfui_form *)dfui_obj,
				    strcmp(value, "true") == 0
				);
			} else if (strcmp(key, "extensible") == 0 &&
			    dfui_obj_type == DFUI_OBJ_FORM) {
				dfui_form_set_extensible(
				    (struct dfui_form *)dfui_obj,
				    strcmp(value, "true") == 0
				);
			} else {
				/*
				 * It's a property.
				 */
				switch (dfui_obj_type) {
				case DFUI_OBJ_FORM:
					dfui_form_property_set(
					    (struct dfui_form *)dfui_obj,
					    key, value);
					break;
				case DFUI_OBJ_FIELD:
					dfui_field_property_set(
					    (struct dfui_field *)dfui_obj,
					    key, value);
					break;
				case DFUI_OBJ_ACTION:
					dfui_action_property_set(
					    (struct dfui_action *)dfui_obj,
					    key, value);
					break;
				}
			}
		} else if (lua_isstring(L, -2) && lua_istable(L, -1)) {
			key = lua_tostring(L, -2);
			if (strcmp(key, "options") == 0 &&
			    dfui_obj_type == DFUI_OBJ_FIELD) {
				dfui_field_options_from_lua_table(L,
				    (struct dfui_field *)dfui_obj
				);
			}
		} else {
			/*
			 * Either the key or the value is not a string,
			 * so just skip it.
			 */
		}

		/*
		 * Remove the value, but leave the key for the next iteration.
		 */
		lua_pop(L, 1);
	}
}

/*** TRANSLATORS ***/

/*
 * Pop a Lua table representing a DFUI action from the Lua stack,
 * create a new DFUI action from it, and return it.
 */
static struct dfui_action *
dfui_action_from_lua_table(lua_State *L, int table_idx)
{
	struct dfui_action *a;
	const char *id, *name, *short_desc, *long_desc;

	/*
	 * Get the basic properties of the action.
	 */
	id = lua_access_table_string(L, table_idx, "id");
	name = lua_access_table_string(L, table_idx, "name");
	short_desc = lua_access_table_string(L, table_idx, "short_desc");
	long_desc = lua_access_table_string(L, table_idx, "long_desc");

	/*
	 * Create the initial action.
	 */
	a = dfui_action_new(id,
	    dfui_info_new(name, short_desc, long_desc));

	set_dfui_properties_from_lua_table(L, table_idx, DFUI_OBJ_ACTION, a);

	lua_pop(L, 1);
	return(a);
}

/*
 * Pop a Lua table representing a DFUI field from the Lua stack,
 * create a new DFUI field from it, and return it.
 */
static struct dfui_field *
dfui_field_from_lua_table(lua_State *L, int table_idx)
{
	struct dfui_field *fi;
	const char *id, *name, *short_desc, *long_desc;

	/*
	 * Get the basic properties of the field.
	 */
	id = lua_access_table_string(L, table_idx, "id");
	name = lua_access_table_string(L, table_idx, "name");
	short_desc = lua_access_table_string(L, table_idx, "short_desc");
	long_desc = lua_access_table_string(L, table_idx, "long_desc");

	/*
	 * Create the initial field.
	 */
	fi = dfui_field_new(id,
	    dfui_info_new(name, short_desc, long_desc));

	set_dfui_properties_from_lua_table(L, table_idx, DFUI_OBJ_FIELD, fi);

	lua_pop(L, 1);
	return(fi);
}

/*
 * Pop a Lua table representing a DFUI dataset from the Lua stack,
 * create a new DFUI dataset from it, and return it.
 */
static struct dfui_dataset *
dfui_dataset_from_lua_table(lua_State *L, int table_idx)
{
	struct dfui_dataset *ds;

	/*
	 * Create the initial dataset.
	 */
	ds = dfui_dataset_new();

	/*
	 * Traverse the table, looking for key->value pairs that we can use.
	 */
	lua_pushnil(L);
	while (lua_next(L, table_idx) != 0) {
		if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
			dfui_dataset_celldata_add(ds,
			    lua_tostring(L, -2), lua_tostring(L, -1)
			);
		} else {
			/* Bogus, just skip it */
		}

		/*
		 * Remove the value, but leave the key for the next iteration.
		 */
		lua_pop(L, 1);
	}	

	/*
	 * Remove the table.
	 */
	lua_pop(L, 1);
	return(ds);
}

/*
 * Pop a Lua table representing a DFUI form from the Lua stack,
 * create a new DFUI form from it, and return it.
 */
static struct dfui_form *
dfui_form_from_lua_table(lua_State *L, int table_idx)
{
	struct dfui_form *f;
	struct dfui_action *a;
	struct dfui_field *fi;
	struct dfui_dataset *ds;
	const char *id, *name, *short_desc, *long_desc;
	int list_idx, subtable_idx, counter, done;

	/*
	 * Get the basic properties of the form.
	 */
	id = lua_access_table_string(L, table_idx, "id");
	name = lua_access_table_string(L, table_idx, "name");
	short_desc = lua_access_table_string(L, table_idx, "short_desc");
	long_desc = lua_access_table_string(L, table_idx, "long_desc");

	/*
	 * Create the initial form.
	 */
	f = dfui_form_new(id, dfui_info_new(name, short_desc, long_desc));

	set_dfui_properties_from_lua_table(L, table_idx, DFUI_OBJ_FORM, f);

	/*
	 * Get the list of actions attached to the form.
	 */
	lua_pushliteral(L, "actions");
	lua_gettable(L, table_idx);
	list_idx = lua_gettop(L);
	if (lua_istable(L, list_idx)) {
		/*
		 * Loop through all entries in this table, creating
		 * and attaching a new action for each one.
		 */
		counter = 1;
		done = 0;
		while (!done) {
			lua_pushnumber(L, counter++);
			lua_gettable(L, list_idx);
			subtable_idx = lua_gettop(L);
			if (lua_istable(L, subtable_idx)) {
				a = dfui_action_from_lua_table(L, subtable_idx);
				dfui_form_action_attach(f, a);
			} else {
				done = 1;
			}
		}
	} else {
		/* No actions */
	}
	lua_pop(L, 1);

	/*
	 * Get the list of fields attached to the form.
	 */
	lua_pushliteral(L, "fields");
	lua_gettable(L, table_idx);
	list_idx = lua_gettop(L);
	if (lua_istable(L, list_idx)) {
		/*
		 * Loop through all entries in this table, creating
		 * and attaching a new field for each one.
		 */
		counter = 1;
		done = 0;
		while (!done) {
			lua_pushnumber(L, counter++);
			lua_gettable(L, list_idx);
			subtable_idx = lua_gettop(L);
			if (lua_istable(L, subtable_idx)) {
				fi = dfui_field_from_lua_table(L, subtable_idx);
				dfui_form_field_attach(f, fi);
			} else {
				done = 1;
			}
		}
	} else {
		/* No fields */
	}
	lua_pop(L, 1);

	/*
	 * Get the list of datasets attached to the form.
	 */
	lua_pushliteral(L, "datasets");
	lua_gettable(L, table_idx);
	list_idx = lua_gettop(L);
	if (lua_istable(L, list_idx)) {
		/*
		 * Loop through all entries in this table, creating
		 * and attaching a new dataset for each one.
		 */
		counter = 1;
		done = 0;
		while (!done) {
			lua_pushnumber(L, counter++);
			lua_gettable(L, list_idx);
			subtable_idx = lua_gettop(L);
			if (lua_istable(L, subtable_idx)) {
				ds = dfui_dataset_from_lua_table(L, subtable_idx);
				dfui_form_dataset_add(f, ds);
			} else {
				done = 1;
			}
		}
	} else {
		/* No datasets */
	}
	lua_pop(L, 1);

	/*
	 * Finally, delete the table representing the form by
	 * popping it from the top of the stack.
	 */
	lua_pop(L, 1);

	return(f);
}

/*
 * Push a new Lua table representing the given DFUI response
 * onto the Lua stack.
 */
static int
lua_table_from_dfui_response(lua_State *L, struct dfui_response *r)
{
	int table_idx, list_idx, subtable_idx;
	struct dfui_dataset *ds;
	struct dfui_celldata *cd;
	const char *value;
	int counter = 1;
	const char *f_id, *a_id;

	lua_newtable(L);
	table_idx = lua_gettop(L);

	/*
	 * Add response id's to the table.
	 */
	f_id = dfui_response_get_form_id(r);
	a_id = dfui_response_get_action_id(r);

	lua_pushliteral(L, "form_id");
	lua_pushlstring(L, f_id, strlen(f_id));
	lua_settable(L, table_idx);

	lua_pushliteral(L, "action_id");
	lua_pushlstring(L, a_id, strlen(a_id));
	lua_settable(L, table_idx);

	/*
	 * Create 'datasets' lists to the table.
	 */
	lua_pushliteral(L, "datasets");
	lua_newtable(L);
	list_idx = lua_gettop(L);
	
	/*
	 * Add response datasets to the 'datasets' list.
	 */
	for (ds = dfui_response_dataset_get_first(r); ds != NULL;
	     ds = dfui_dataset_get_next(ds)) {
		lua_pushnumber(L, counter++);
		lua_newtable(L);
		subtable_idx = lua_gettop(L);
		/*
		 * Populate this subtable with the celldatas...
		 */
		for (cd = dfui_dataset_celldata_get_first(ds); cd != NULL;
		     cd = dfui_celldata_get_next(cd)) {
			f_id = dfui_celldata_get_field_id(cd);
			value = dfui_celldata_get_value(cd);
			lua_pushlstring(L, f_id, strlen(f_id));
			lua_pushlstring(L, value, strlen(value));
			lua_settable(L, subtable_idx);
		}
		/*
		 * Add this subtable to the list
		 */
		lua_settable(L, list_idx);
	}

	/*
	 * Add the 'datasets' list to the table.
	 */
	lua_settable(L, table_idx);

	return(table_idx);
}

/*** CONSTRUCTOR & DESTRUCTOR ***/

static int
lua_dfui_connection_new(lua_State *L)
{
	const char *transport_string, *rendezvous;
	int transport;
	struct dfui_connection *c;

	transport_string = luaL_checkstring(L, 1);
	rendezvous = luaL_checkstring(L, 2);
	if (! (transport = get_transport(transport_string)) > 0) {
		lua_pushnil(L);
		return(1);
	}
	c = dfui_connection_new(transport, rendezvous);
	lua_push_dfui_connection(L, c);
	return(1);
}

static int
lua_dfui_connection_destroy(lua_State *L)
{
	struct dfui_connection *c;

	c = (struct dfui_connection *)lua_unboxpointer(L, 1);
	if (c != NULL) {
		dfui_be_stop(c);
		dfui_connection_free(c);
	}
	return(0);
}

/*** BOUND METHODS ***/

static int
lua_dfui_be_start(lua_State *L)
{
	struct dfui_connection *c;
	int result;

	c = lua_check_dfui_connection(L, 1);
	result = dfui_be_start(c);
	lua_pushnumber(L, result);

	return(1);
}

static int
lua_dfui_be_stop(lua_State *L)
{
	struct dfui_connection *c;
	int result;

	c = lua_check_dfui_connection(L, 1);
	result = dfui_be_stop(c);
	lua_pushnumber(L, result);

	return(1);
}

static int
lua_dfui_be_present(lua_State *L)
{
	struct dfui_connection *c;
	struct dfui_form *f;
	struct dfui_response *r;
	int response_table_idx, actions_list_idx, action_table_idx;
	const char *a_a_id, *r_a_id;

	c = lua_check_dfui_connection(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	f = dfui_form_from_lua_table(L, 2);

	if (dfui_be_present(c, f, &r)) {
		response_table_idx = lua_table_from_dfui_response(L, r);

		r_a_id = dfui_response_get_action_id(r);

		/*
		 * Handle the 'effect' key which may be given in
		 * any action table within a form table.  When it
		 * is given, it should be a function which the
		 * user wishes to be executed automatically when
		 * the response is caused by that action.  This lets
		 * the user write simpler Lua code (c:present(f) can
		 * execute things directly, instead of returning an
		 * id code which the user must look up in a table etc.)
		 */
		/*
		 * First, look for an 'actions' list in the form table.
		 */
		lua_pushliteral(L, "actions");
		lua_gettable(L, 2);
		actions_list_idx = lua_gettop(L);
		if (lua_istable(L, actions_list_idx)) {
			int i = 1;
			int done = 0;

			/*
			 * Look in the 'actions' list for
			 * action tables.
			 */
			while (!done) {
				lua_rawgeti(L, actions_list_idx, i);
				action_table_idx = lua_gettop(L);
				if (lua_istable(L, action_table_idx)) {
					/*
					 * See if this action's 'id'
					 * is the response's action_id
					 * (which we saved above.)
					 */
					a_a_id = lua_access_table_string(L,
					    action_table_idx, "id");
					if (strcmp(r_a_id, a_a_id) == 0) {
						/*
						 * It is.  So, see if action
						 * table has an 'effect' key.
						 */
						lua_pushliteral(L, "result");
						lua_pushliteral(L, "effect");
						lua_gettable(L, action_table_idx);
						if (lua_isfunction(L, lua_gettop(L))) {
							/*
							 * It is, and it's a function.
							 * Execute it.
							 */
							lua_call(L, 0, 1);
							lua_rawset(L, response_table_idx);
							done = 1;
						} else {
							lua_pop(L, 2);
						}
					}
				} else {
					done = 1;
				}
				lua_pop(L, 1);	/* remove the action table */
				i++;
			}
		}
		lua_pop(L, 1);	/* remove the 'actions' list */
	} else {
		lua_pushnil(L);
	}

	dfui_response_free(r);
	dfui_form_free(f);
	return(1);
}

/**** Binding Tables ****/

const luaL_reg dfui_connection_methods[] = {
	{"new",		lua_dfui_connection_new },
	{"start",	lua_dfui_be_start },
	{"stop",	lua_dfui_be_stop },
	{"present",	lua_dfui_be_present },
	{0, 0}
};

const luaL_reg dfui_connection_meta_methods[] = {
	{"__gc",	lua_dfui_connection_destroy },
	{0, 0}
};


/*** REGISTRATION ***/

LUA_API int
lua_dfui_register(lua_State *L)
{
	luaL_openlib(L, "DFUIConnection",
			dfui_connection_methods, 0);	    /* fill methods table */
        luaL_openlib(L, "DFUIConnectionMeta",
			dfui_connection_meta_methods,  0);  /* fill metatable */
	lua_pop(L, 1);

	lua_set_instance_handler(L,
			"DFUIConnection", "DFUIConnectionMeta");

	return(1);
}
