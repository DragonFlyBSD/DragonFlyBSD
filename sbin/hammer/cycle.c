/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cycle.c,v 1.2 2008/05/12 21:17:16 dillon Exp $
 */

#include "hammer.h"

int64_t
hammer_get_cycle(int64_t default_obj_id)
{
	int64_t obj_id;
	FILE *fp;

	if (CyclePath && (fp = fopen(CyclePath, "r")) != NULL) {
		if (fscanf(fp, "%llx\n", &obj_id) != 1) {
			obj_id = default_obj_id;
			fprintf(stderr, "Warning: malformed obj_id in %s\n",
				CyclePath);
		}
		fclose(fp);
	} else {
		obj_id = default_obj_id;
	}
	return(obj_id);
}

void
hammer_set_cycle(int64_t obj_id)
{
	FILE *fp;

	if ((fp = fopen(CyclePath, "w")) != NULL) {
		fprintf(fp, "%016llx\n", obj_id);
		fclose(fp);
	} else {
		fprintf(stderr, "Warning: Unable to write to %s: %s\n",
			CyclePath, strerror(errno));
	}
}

void
hammer_reset_cycle(void)
{
	remove(CyclePath);
}

