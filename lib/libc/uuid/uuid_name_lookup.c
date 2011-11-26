/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/lib/libc/uuid/uuid_name_lookup.c,v 1.6 2008/01/07 01:22:30 corecode Exp $
 */
/*
 * Implement UUID-to-NAME and NAME-to-UUID functions
 */

#include <sys/types.h>
#include <sys/tree.h>
#include <uuid.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Implement a Red-Black tree to cache the UUID table and perform lookups
 */
struct uuid_rbnode {
	RB_ENTRY(uuid_rbnode) unode;
	RB_ENTRY(uuid_rbnode) nnode;
	struct uuid uuid;
	char *name;
};

static void uuid_loadcache(const char *path);

static int uuid_name_loaded;

RB_HEAD(uuid_urbtree, uuid_rbnode);
RB_PROTOTYPE_STATIC(uuid_urbtree, uuid_rbnode, unode, uuid_urbcmp);
static struct uuid_urbtree uuid_urbroot = RB_INITIALIZER(uuid_urbroot);

RB_HEAD(uuid_nrbtree, uuid_rbnode);
RB_PROTOTYPE_STATIC(uuid_nrbtree, uuid_rbnode, nnode, uuid_nrbcmp);
static struct uuid_nrbtree uuid_nrbroot = RB_INITIALIZER(uuid_nrbroot);

static int
uuid_urbcmp(struct uuid_rbnode *n1, struct uuid_rbnode *n2)
{
	return(uuid_compare(&n1->uuid, &n2->uuid, NULL));
}

static int
uuid_nrbcmp(struct uuid_rbnode *n1, struct uuid_rbnode *n2)
{
	return(strcasecmp(n1->name, n2->name));
}

static int
uuid_rbnamecmp(const char *name, struct uuid_rbnode *node)
{
	return (strcasecmp(name, node->name));
}

static int
uuid_rbuuidcmp(const struct uuid *uuid, struct uuid_rbnode *node)
{
	return(uuid_compare(uuid, &node->uuid, NULL));
}

RB_GENERATE_STATIC(uuid_urbtree, uuid_rbnode, unode, uuid_urbcmp)
RB_GENERATE_STATIC(uuid_nrbtree, uuid_rbnode, nnode, uuid_nrbcmp)
RB_GENERATE_XLOOKUP_STATIC(uuid_urbtree, UUID, uuid_rbnode, unode,
			   uuid_rbuuidcmp, const struct uuid *)
RB_GENERATE_XLOOKUP_STATIC(uuid_nrbtree, NAME, uuid_rbnode, nnode,
			   uuid_rbnamecmp, const char *)
		

/*
 * Look up a UUID by its address.  Returns 0 on success or an error
 */
void
uuid_addr_lookup(const uuid_t *u, char **strp, uint32_t *status)
{
	struct uuid_rbnode *node;

	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
	if (u) {
		if (uuid_name_loaded == 0) {
			/*
			 * /etc/uuids will override /etc/defaults/uuids
			 */
			uuid_loadcache("/etc/uuids");
			uuid_loadcache("/etc/defaults/uuids");
			uuid_name_loaded = 1;
		}
		node = uuid_urbtree_RB_LOOKUP_UUID(&uuid_urbroot, u);
		if (node) {
			*strp = strdup(node->name);
			if (status)
				*status = uuid_s_ok;
			return;
		}
	}
	if (status)
		*status = uuid_s_not_found;
}

/*
 * Look up a UUID by its name.  Returns 0 on success or an error.
 */
void
uuid_name_lookup(uuid_t *u, const char *name, uint32_t *status)
{
	struct uuid_rbnode *node;

	if (name) {
		if (uuid_name_loaded == 0) {
			uuid_loadcache("/etc/uuids");
			uuid_loadcache("/etc/defaults/uuids");
			uuid_name_loaded = 1;
		}
		node = uuid_nrbtree_RB_LOOKUP_NAME(&uuid_nrbroot, name);
		if (node) {
			if (u)
				*u = node->uuid;
			if (status)
				*status = uuid_s_ok;
			return;
		}
	}
	if (u)
		bzero(u, sizeof(*u));
	if (status)
		*status = uuid_s_not_found;
}

/*
 * Clear out the lookup cache.  The next lookup will reload the database
 * or re-query or whatever.
 */
static
int
uuid_freenode(struct uuid_rbnode *node, void *arg __unused)
{
	uuid_urbtree_RB_REMOVE(&uuid_urbroot, node);
	uuid_nrbtree_RB_REMOVE(&uuid_nrbroot, node);
	free(node->name);
	free(node);
	return (0);
}

void
uuid_reset_lookup(void)
{
	uuid_urbtree_RB_SCAN(&uuid_urbroot, NULL, uuid_freenode, NULL);
	uuid_name_loaded = 0;
}

static
void
uuid_loadcache(const char *path)
{
	struct uuid_rbnode *node;
	uint32_t status;
	FILE *fp;
	char *line;
	char *uuid;
	char *name;
	char *last;
	size_t len;

	if ((fp = fopen(path, "r")) == NULL)
		return;
	while ((line = fgetln(fp, &len)) != NULL) {
		if (len == 0 || *line == '#')
			continue;
		line[len-1] = 0;
		uuid = strtok_r(line, " \t\r", &last);
		if (uuid == NULL)
			continue;
		name = strtok_r(NULL, "", &last);
		name = strchr(name, '"');
		if (name == NULL)
			continue;
		*name++ = 0;
		if (strchr(name, '"') == NULL)
			continue;
		*strchr(name, '"') = 0;
		node = malloc(sizeof(*node));
		node->name = strdup(name);
		uuid_from_string(uuid, &node->uuid, &status);
		if (status == 0) {
			if (uuid_urbtree_RB_FIND(&uuid_urbroot, node) ||
			    uuid_nrbtree_RB_FIND(&uuid_nrbroot, node))
				status = 1;
		}
		if (status == 0) {
			uuid_urbtree_RB_INSERT(&uuid_urbroot, node);
			uuid_nrbtree_RB_INSERT(&uuid_nrbroot, node);
		} else {
			free(node->name);
			free(node);
		}
	}
	fclose(fp);
}


