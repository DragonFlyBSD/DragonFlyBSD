/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 */

#include <stand.h>
#include <string.h>
#include "bootstrap.h"
#include "dloader.h"

dvar_t dvbase;
dvar_t *dvlastp = &dvbase;

dvar_t
dvar_get(const char *name)
{
	dvar_t var;

	for (var = dvbase; var; var = var->next) {
		if (strcmp(name, var->name) == 0)
			return(var);
	}
	return(NULL);
}

void
dvar_set(const char *name, char **data, int count)
{
	dvar_t var;

	for (var = dvbase; var; var = var->next) {
		if (strcmp(name, var->name) == 0)
			break;
	}
	if (var == NULL) {
		var = malloc(sizeof(*var) + strlen(name) + 1);
		var->name = (char *)(void *)(var + 1);
		strcpy(var->name, name);
		var->next = NULL;
		*dvlastp = var;
		dvlastp = &var->next;
	} else {
		while (--var->count >= 0)
			free(var->data[var->count]);
		free(var->data);
		/* var->data = NULL; not needed */
	}
	var->count = count;
	var->data = malloc(sizeof(char *) * (count + 1));
	var->data[count] = NULL;
	while (--count >= 0)
		var->data[count] = strdup(data[count]);
}

dvar_t
dvar_copy(dvar_t ovar)
{
	dvar_t var;
	int count;

	var = malloc(sizeof(*var));
	bzero(var, sizeof(*var));
	count = ovar->count;

	var->count = count;
	var->data = malloc(sizeof(char *) * (count + 1));
	var->data[count] = NULL;
	while (--count >= 0)
		var->data[count] = strdup(ovar->data[count]);
	return (var);
}

void
dvar_unset(const char *name)
{
	dvar_t *lastp;
	dvar_t var;
	char *p;

	lastp = &dvbase;
	if ((p = strchr(name, '*')) != NULL) {
		while ((var = *lastp) != NULL) {
			if (strlen(var->name) >= p - name &&
			    strncmp(var->name, name, p - name) == 0) {
				dvar_free(lastp);
			} else {
				lastp = &var->next;
			}
		}
	} else {
		while ((var = *lastp) != NULL) {
			if (strcmp(name, var->name) == 0) {
				dvar_free(lastp);
				break;
			}
			lastp = &var->next;
		}
	}
}

dvar_t
dvar_first(void)
{
	return(dvbase);
}

dvar_t
dvar_next(dvar_t var)
{
	return(var->next);
}

dvar_t *
dvar_firstp(void)
{
	return(&dvbase);
}

dvar_t *
dvar_nextp(dvar_t var)
{
	return(&var->next);
}

void
dvar_free(dvar_t *lastp)
{
	dvar_t dvar = *lastp;

	if (dvlastp == &dvar->next)
		dvlastp = lastp;
	*lastp = dvar->next;
	while (--dvar->count >= 0)
		free(dvar->data[dvar->count]);
	free(dvar->data);
	free(dvar);
}

int
dvar_istrue(dvar_t var)
{
	int retval = 0;

	if (var != NULL && (strcasecmp(var->data[0], "yes") == 0 ||
	    strcasecmp(var->data[0], "true") == 0 ||
	    strcasecmp(var->data[0], "on") == 0 ||
	    strcasecmp(var->data[0], "1") == 0))
		retval = 1;

	return (retval);
}
