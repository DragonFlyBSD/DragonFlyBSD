/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/types.h>
#include <sys/device.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <sys/un.h>
#include <cpu/inttypes.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>

#include <libprop/proplib.h>
#include <sys/udev.h>
#include "udevd.h"

static int64_t udev_generation;
TAILQ_HEAD(pdev_array_list_head, pdev_array_entry)	pdev_array_list;

void
pdev_array_entry_ref(struct pdev_array_entry *pae)
{
	++pae->refs;
}

void
pdev_array_entry_unref(struct pdev_array_entry *pae)
{
	if (--pae->refs == 0) {
		TAILQ_REMOVE(&pdev_array_list, pae, link);
		prop_object_release(pae->pdev_array); /* XXX */
		free(pae);
	}
}

void
pdev_array_entry_insert(prop_array_t pa)
{
	struct pdev_array_entry *pae, *opae = NULL;

	if (pa == NULL)
		errx(1, "null prop_array in insert_pdev_array");
	pae = malloc(sizeof(struct pdev_array_entry));
	if (pae == NULL)
		errx(1, "insert_pdev_array could not allocate mem");
	memset(pae, 0, sizeof(struct pdev_array_entry));
	pae->pdev_array = pa;
	pae->generation = udev_generation++;
	pae->refs = 1; /* One ref because it's the most recent one */

	/*
	 * If the TAILQ is not empty, unref the last entry,
	 * as it isn't needed anymore.
	 */
	if (!TAILQ_EMPTY(&pdev_array_list))
		opae = TAILQ_LAST(&pdev_array_list, pdev_array_list_head);

	TAILQ_INSERT_TAIL(&pdev_array_list, pae, link);

	if (opae != NULL)
		pdev_array_entry_unref(opae);
}

void
pdev_array_clean(void)
{
	struct pdev_array_entry *pae;

	while ((pae = TAILQ_LAST(&pdev_array_list, pdev_array_list_head)) != NULL) {
		TAILQ_REMOVE(&pdev_array_list, pae, link);
		prop_object_release(pae->pdev_array);
		free(pae);
	}
}

struct pdev_array_entry *
pdev_array_entry_get(int64_t generation)
{
	struct pdev_array_entry *pae;
	int found = 0;

	TAILQ_FOREACH(pae, &pdev_array_list, link) {
		if (pae->generation == generation) {
			found = 1;
			break;
		}
	}

	if (!found)
		return NULL;

	pdev_array_entry_ref(pae);
	return pae;
}

struct pdev_array_entry *
pdev_array_entry_get_last(void)
{
	struct pdev_array_entry *pae;

	pae = TAILQ_LAST(&pdev_array_list, pdev_array_list_head);

	pdev_array_entry_ref(pae);
	return pae;
}
