/*-
 * Copyright (c) 2017 The DragonFly Project
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <vfs/hammer2/hammer2_disk.h>

#include "fstyp.h"

static hammer2_volume_data_t*
__read_voldata(FILE *fp)
{
	hammer2_volume_data_t *voldata;

	voldata = read_buf(fp, 0, sizeof(*voldata));
	if (voldata == NULL)
		err(1, "failed to read volume data");

	return (voldata);
}

static int
__test_voldata(const hammer2_volume_data_t *voldata)
{
	if (voldata->magic != HAMMER2_VOLUME_ID_HBO &&
	    voldata->magic != HAMMER2_VOLUME_ID_ABO)
		return (1);

	return (0);
}

int
fstyp_hammer2(FILE *fp, char *label, size_t size)
{
	hammer2_volume_data_t *voldata;
	int error = 1;

	voldata = __read_voldata(fp);
	if (__test_voldata(voldata))
		goto done;

	// XXX -l option not supported yet
	//strlcpy(label, "label", size);

	error = 0;
done:
	free(voldata);
	return (error);
}
