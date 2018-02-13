/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

#include "hammer_util.h"

#include <uuid.h>

void hammer_uuid_create(hammer_uuid_t *uuid)
{
	uuid_create(uuid, NULL);
}

int hammer_uuid_from_string(const char *str, hammer_uuid_t *uuid)
{
	uint32_t status = uuid_s_ok;

	uuid_from_string(str, uuid, &status);

	if (status != uuid_s_ok)
		return(-1);
	return(0);
}

int hammer_uuid_to_string(const hammer_uuid_t *uuid, char **str)
{
	uint32_t status = uuid_s_ok;

	uuid_to_string(uuid, str, &status);

	if (status != uuid_s_ok)
		return(-1);
	return(0);
}

int hammer_uuid_name_lookup(hammer_uuid_t *uuid, const char *str)
{
	uint32_t status = uuid_s_ok;

	uuid_name_lookup(uuid, str, &status);

	if (status != uuid_s_ok)
		return(-1);
	return(0);
}

int hammer_uuid_compare(const hammer_uuid_t *uuid1, const hammer_uuid_t *uuid2)
{
	return(uuid_compare(uuid1, uuid2, NULL));
}
