/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * diskutil.c
 * Disk utility functions for installer.
 * $Id: diskutil.c,v 1.44 2005/02/07 06:41:42 cpressey Exp $
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libaura/mem.h"
#include "libaura/fspred.h"
#include "libaura/popen.h"

#include "libdfui/dfui.h"
#include "libdfui/dump.h"

#define NEEDS_DISKUTIL_STRUCTURE_DEFINITIONS
#include "diskutil.h"
#undef NEEDS_DISKUTIL_STRUCTURE_DEFINITIONS

#include "commands.h"
#include "functions.h"
#include "sysids.h"
#include "uiutil.h"

static int	disk_description_is_better(const char *, const char *);

/** STORAGE DESCRIPTORS **/

struct storage *
storage_new(void)
{
	struct storage *s;

	AURA_MALLOC(s, storage);

	s->disk_head = NULL;
	s->disk_tail = NULL;
	s->selected_disk = NULL;
	s->selected_slice = NULL;
	s->ram = -1;

	return(s);
}

int
storage_get_tmpfs_status(const char *mountpoint, struct storage *s)
{
	struct subpartition *sp;
	sp = NULL;
	for (sp = slice_subpartition_first(s->selected_slice);
		sp != NULL; sp = subpartition_next(sp)) {
		if(strcmp(subpartition_get_mountpoint(sp), mountpoint) == 0) {
			if(subpartition_is_tmpfsbacked(sp) == 1) {
				return 1;
			} else {
				return 0;
			}
		}
	}
	return 0;
}

void
storage_free(struct storage *s)
{
	disks_free(s);
	AURA_FREE(s, storage);
}

void
storage_set_memsize(struct storage *s, unsigned long memsize)
{
	s->ram = memsize;
}

long
storage_get_memsize(const struct storage *s)
{
	return(s->ram);
}

struct disk *
storage_disk_first(const struct storage *s)
{
	return(s->disk_head);
}

void
storage_set_selected_disk(struct storage *s, struct disk *d)
{
	s->selected_disk = d;
}

struct disk *
storage_get_selected_disk(const struct storage *s)
{
	return(s->selected_disk);
}

void
storage_set_selected_slice(struct storage *s, struct slice *sl)
{
	s->selected_slice = sl;
}

struct slice *
storage_get_selected_slice(const struct storage *s)
{
	return(s->selected_slice);
}

/*
 * Create a new disk description structure.
 */
struct disk *
disk_new(struct storage *s, const char *dev_name)
{
	struct disk *d;

	AURA_MALLOC(d, disk);

	d->device = aura_strdup(dev_name);
	d->desc = NULL;
	d->serno = NULL;
	d->we_formatted = 0;
	d->capacity = 0;

	d->cylinders = -1;	/* -1 indicates "we don't know" */
	d->heads = -1;
	d->sectors = -1;

	d->slice_head = NULL;
	d->slice_tail = NULL;

	d->next = NULL;
	if (s->disk_head == NULL)
		s->disk_head = d;
	else
		s->disk_tail->next = d;

	d->prev = s->disk_tail;
	s->disk_tail = d;

	return(d);
}

static int
disk_description_is_better(const char *existing, const char *new_desc __unused)
{
	if (existing == NULL)
		return(1);
	return(0);
}

const char *
disk_get_desc(const struct disk *d)
{
	return(d->desc);
}

void
disk_set_desc(struct disk *d, const char *desc)
{
	char *c;

	if (!disk_description_is_better(d->desc, desc))
		return;
	if (d->desc != NULL)
		free(d->desc);
	d->desc = aura_strdup(desc);

	/*
	 * Get the disk's total capacity.
	 * XXX we should do this with C/H/S ?
	 */
	c = d->desc;
	while (*c != ':' && *c != '\0')
		c++;
	if (*c == '\0')
		d->capacity = 0;
	else
		d->capacity = atoi(c + 1);
}

/*
 * Returns the name of the device node used to represent the disk.
 * Note that the storage used for the returned string is static,
 * and the string is overwritten each time this function is called.
 */
const char *
disk_get_device_name(const struct disk *d)
{
	static char tmp_dev_name[256];

	snprintf(tmp_dev_name, 256, "%s", d->device);
	return(tmp_dev_name);
}

const char *
disk_get_serno(const struct disk *d)
{
	return(d->serno);
}

void
disk_set_serno(struct disk *d, const char *serno)
{
	d->serno = aura_strdup(serno);
}

int
disk_get_number(const struct disk *d)
{
	return(d->number);
}

void
disk_set_number(struct disk *d, const int number)
{
	d->number = number;
}

/*
 * Find the first disk description structure in the given
 * storage description which matches the given device name
 * prefix.  Note that this means that if a storage
 * description s contains disks named "ad0" and "ad1",
 * disk_find(s, "ad0s1c") will return a pointer to the disk
 * structure for "ad0".
 */
struct disk *
disk_find(const struct storage *s, const char *device)
{
	struct disk *d = s->disk_head;

	while (d != NULL) {
		if (strncmp(device, d->device, strlen(d->device)) == 0 &&
		    strlen(device) == strlen(d->device))
			return(d);
		d = d->next;
	}

	return(NULL);
}

struct disk *
disk_next(const struct disk *d)
{
	return(d->next);
}

struct slice *
disk_slice_first(const struct disk *d)
{
	return(d->slice_head);
}

void
disk_set_formatted(struct disk *d, int formatted)
{
	d->we_formatted = formatted;
}

int
disk_get_formatted(const struct disk *d)
{
	return(d->we_formatted);
}

void
disk_set_geometry(struct disk *d, int cyl, int hd, int sec)
{
	d->cylinders = cyl;
	d->heads = hd;
	d->sectors = sec;
}

void
disk_get_geometry(const struct disk *d, int *cyl, int *hd, int *sec)
{
	*cyl = d->cylinders;
	*hd = d->heads;
	*sec = d->sectors;
}

/*
 * Free the memory allocated to hold the set of disk descriptions.
 */
void
disks_free(struct storage *s)
{
	struct disk *d = s->disk_head, *next;

	while (d != NULL) {
		next = d->next;
		slices_free(d->slice_head);
		free(d->desc);
		free(d->device);
		AURA_FREE(d, disk);
		d = next;
	}

	s->disk_head = NULL;
	s->disk_tail = NULL;
}

/*
 * Create a new slice description and add it to a disk description.
 */
struct slice *
slice_new(struct disk *d, int number, int type, int flags,
	  unsigned long start, unsigned long size)
{
	struct slice *s;
	const char *sysid_desc = NULL;
	char unknown[256];
	int i;

	dfui_debug("** adding slice %d (start %ld, size %ld, sysid %d) "
	    "to disk %s\n", number, start, size, type, d->device);

	AURA_MALLOC(s, slice);

	s->parent = d;

	s->subpartition_head = NULL;
	s->subpartition_tail = NULL;
	s->number = number;

	s->type = type;
	s->flags = flags;
	s->start = start;
	s->size = size;

	for (i = 0; ; i++) {
		if (part_types[i].type == type) {
			sysid_desc = part_types[i].name;
			break;
		}
		if (part_types[i].type == 255)
			break;
	}
	if (sysid_desc == NULL) {
		snprintf(unknown, 256, "??? Unknown, sysid = %d", type);
		sysid_desc = unknown;
	}

	asprintf(&s->desc, "%ldM - %ldM: %s",
	    start / 2048, (start + size) / 2048, sysid_desc);
	s->capacity = size / 2048;

	s->next = NULL;
	if (d->slice_head == NULL)
		d->slice_head = s;
	else
		d->slice_tail->next = s;

	s->prev = d->slice_tail;
	d->slice_tail = s;

	return(s);
}

/*
 * Find a slice description on a given disk description given the
 * slice number.
 */
struct slice *
slice_find(const struct disk *d, int number)
{
	struct slice *s = d->slice_head;

	while (s != NULL) {
		if (s->number == number)
			return(s);
		s = s->next;
	}

	return(NULL);
}

struct slice *
slice_next(const struct slice *s)
{
	return(s->next);
}

/*
 * Returns the name of the device node used to represent the slice.
 * Note that the storage used for the returned string is static,
 * and the string is overwritten each time this function is called.
 */
const char *
slice_get_device_name(const struct slice *s)
{
	static char tmp_dev_name[256];

	snprintf(tmp_dev_name, 256, "%ss%d", s->parent->device, s->number);
	return(tmp_dev_name);
}

int
slice_get_number(const struct slice *s)
{
	return(s->number);
}

const char *
slice_get_desc(const struct slice *s)
{
	return(s->desc);
}

unsigned long
slice_get_capacity(const struct slice *s)
{
	return(s->capacity);
}

unsigned long
slice_get_start(const struct slice *s)
{
	return(s->start);
}

unsigned long
slice_get_size(const struct slice *s)
{
	return(s->size);
}

int
slice_get_type(const struct slice *s)
{
	return(s->type);
}

int
slice_get_flags(const struct slice *s)
{
	return(s->flags);
}

struct subpartition *
slice_subpartition_first(const struct slice *s)
{
	return(s->subpartition_head);
}

/*
 * Free all memory for a list of slice descriptions.
 */
void
slices_free(struct slice *head)
{
	struct slice *next;

	while (head != NULL) {
		next = head->next;
		subpartitions_free(head);
		free(head->desc);
		AURA_FREE(head, slice);
		head = next;
	}
}

struct subpartition *
subpartition_new_hammer(struct slice *s, const char *mountpoint, long capacity,
    int encrypted)
{
	struct subpartition *sp;

	AURA_MALLOC(sp, subpartition);

	sp->parent = s;

	struct subpartition *last = s->subpartition_tail;
	if (last == NULL) {
		sp->letter = 'a';
	} else if (last->letter == 'b') {
		sp->letter = 'd';
	} else {
		sp->letter = (char)(last->letter + 1);
	}

	sp->mountpoint = aura_strdup(mountpoint);
	sp->capacity = capacity;
	sp->encrypted = encrypted;
	sp->type = FS_HAMMER;

	/*
	 * We need this here, because a UFS /boot needs valid values
	 */
	if (sp->capacity < 1024)
		sp->fsize = 1024;
	else
		sp->fsize = 2048;

	if (sp->capacity < 1024)
		sp->bsize = 8192;
	else
		sp->bsize = 16384;

	sp->is_swap = 0;
	sp->pfs = 0;
	if (strcasecmp(mountpoint, "swap") == 0)
		sp->is_swap = 1;
	if (strcmp(mountpoint, "/") != 0 && strcmp(mountpoint, "/boot") != 0 &&
	    strcmp(mountpoint, "swap") != 0)
		sp->pfs = 1;

	sp->next = NULL;
	if (s->subpartition_head == NULL)
		s->subpartition_head = sp;
	else
		s->subpartition_tail->next = sp;

	sp->prev = s->subpartition_tail;
	s->subpartition_tail = sp;

	return(sp);
}

/*
 * NOTE: arguments to this function are not checked for sanity.
 *
 * fsize and/or bsize may both be -1, indicating
 * "choose a reasonable default."
 */
struct subpartition *
subpartition_new_ufs(struct slice *s, const char *mountpoint, long capacity,
    int encrypted, int softupdates, long fsize, long bsize, int tmpfsbacked)
{
	struct subpartition *sp, *sptmp;
	int letter='d';

	AURA_MALLOC(sp, subpartition);

	sp->parent = s;

	sp->mountpoint = aura_strdup(mountpoint);
	sp->capacity = capacity;
	sp->encrypted = encrypted;
	sp->type = FS_UFS;

	if (fsize == -1) {
		if (sp->capacity < 1024)
			sp->fsize = 1024;
		else
			sp->fsize = 2048;
	} else {
		sp->fsize = fsize;
	}

	if (bsize == -1) {
		if (sp->capacity < 1024)
			sp->bsize = 8192;
		else
			sp->bsize = 16384;
	} else {
		sp->bsize = bsize;
	}

	if (softupdates == -1) {
		if (strcmp(mountpoint, "/") == 0)
			sp->softupdates = 0;
		else
			sp->softupdates = 1;
	} else {
		sp->softupdates = softupdates;
	}

	sp->tmpfsbacked = tmpfsbacked;

	sp->is_swap = 0;
	if (strcasecmp(mountpoint, "swap") == 0)
		sp->is_swap = 1;

	if (s->subpartition_head == NULL) {
		s->subpartition_head = sp;
		s->subpartition_tail = sp;
	} else {
		for (sptmp = s->subpartition_head; sptmp != NULL;
		     sptmp = sptmp->next) {
			if (strcmp(sptmp->mountpoint, sp->mountpoint) > 0)
				break;
		}
		if (sptmp != NULL) {
			if (s->subpartition_head == sptmp)
				s->subpartition_head = sp;
			else
				sptmp->prev->next = sp;
			sp->next = sptmp;
			sp->prev = sptmp->prev;
			sptmp->prev = sp;
		} else {
			sp->prev = s->subpartition_tail;
			s->subpartition_tail->next = sp;
			s->subpartition_tail = sp;
		}
	}

	for (sptmp = s->subpartition_head; sptmp != NULL;
	     sptmp = sptmp->next) {
		if (sptmp->tmpfsbacked)
			sptmp->letter = '@';
		else if (strcmp(sptmp->mountpoint, "/") == 0 ||
			 strcmp(sptmp->mountpoint, "/dummy") == 0)
			sptmp->letter = 'a';
		else if (strcasecmp(sptmp->mountpoint, "swap") == 0)
			sptmp->letter = 'b';
		else
			sptmp->letter = letter++;
	}

	return(sp);
}

/*
 * Find the subpartition description in the given storage
 * description whose mountpoint matches the given string exactly.
 */
struct subpartition *
subpartition_find(const struct slice *s, const char *fmt, ...)
{
	struct subpartition *sp = s->subpartition_head;
	char *mountpoint;
	va_list args;

	va_start(args, fmt);
	vasprintf(&mountpoint, fmt, args);
	va_end(args);

	while (sp != NULL) {
		if (strcmp(mountpoint, sp->mountpoint) == 0) {
			free(mountpoint);
			return(sp);
		}
		sp = sp->next;
	}

	free(mountpoint);
	return(NULL);
}

/*
 * Find the subpartition description in the given storage
 * description where the given filename would presumably
 * reside.  This is the subpartition whose mountpoint is
 * the longest match for the given filename.
 */
struct subpartition *
subpartition_of(const struct slice *s, const char *fmt, ...)
{
	struct subpartition *sp = s->subpartition_head;
	struct subpartition *csp = NULL;
	size_t len = 0;
	char *filename;
	va_list args;

	va_start(args, fmt);
	vasprintf(&filename, fmt, args);
	va_end(args);

	while (sp != NULL) {
		if (strlen(sp->mountpoint) > len &&
		    strlen(sp->mountpoint) <= strlen(filename) &&
		    strncmp(filename, sp->mountpoint, strlen(sp->mountpoint)) == 0) {
				csp = sp;
				len = strlen(csp->mountpoint);
		}
		sp = sp->next;
	}

	free(filename);
	return(csp);
}

struct subpartition *
subpartition_find_capacity(const struct slice *s, long capacity)
{
	struct subpartition *sp = s->subpartition_head;

	while (sp != NULL) {
		if (sp->capacity == capacity)
			return(sp);
		sp = sp->next;
	}

	return(NULL);
}

struct subpartition *
subpartition_next(const struct subpartition *sp)
{
	return(sp->next);
}

int
subpartition_get_pfs(const struct subpartition *sp)
{
	return(sp->pfs);
}

/*
 * Returns the name of the device node used to represent
 * the subpartition, either by serial number or traditional style.
 * Note that the storage used for the returned string is static,
 * and the string is overwritten each time this function is called.
 */
const char *
subpartition_get_device_name(const struct subpartition *sp)
{
	static char tmp_dev_name[256];

	if (sp->parent->parent->serno != NULL)
		snprintf(tmp_dev_name, 256, "serno/%s.s%d%c",
		    sp->parent->parent->serno, sp->parent->number, sp->letter);
	else
		snprintf(tmp_dev_name, 256, "%ss%d%c",
		    sp->parent->parent->device, sp->parent->number, sp->letter);
	return(tmp_dev_name);
}

const char *
subpartition_get_mountpoint(const struct subpartition *sp)
{
	return(sp->mountpoint);
}

char
subpartition_get_letter(const struct subpartition *sp)
{
	return(sp->letter);
}

unsigned long
subpartition_get_fsize(const struct subpartition *sp)
{
	return(sp->fsize);
}

unsigned long
subpartition_get_bsize(const struct subpartition *sp)
{
	return(sp->bsize);
}

long
subpartition_get_capacity(const struct subpartition *sp)
{
	return(sp->capacity);
}

void
subpartition_clr_encrypted(struct subpartition *sp)
{
	sp->encrypted = 0;
}

int
subpartition_is_encrypted(const struct subpartition *sp)
{
	return(sp->encrypted);
}

int
subpartition_is_swap(const struct subpartition *sp)
{
	return(sp->is_swap);
}

int
subpartition_is_softupdated(const struct subpartition *sp)
{
	return(sp->softupdates);
}
int
subpartition_is_tmpfsbacked(const struct subpartition *sp)
{
	return(sp->tmpfsbacked);
}

int
subpartition_count(const struct slice *s)
{
	struct subpartition *sp = s->subpartition_head;
	int count = 0;

	while (sp != NULL) {
		count++;
		sp = sp->next;
	}

	return(count);
}

void
subpartitions_free(struct slice *s)
{
	struct subpartition *sp = s->subpartition_head, *next;

	while (sp != NULL) {
		next = sp->next;
		free(sp->mountpoint);
		AURA_FREE(sp, subpartition);
		sp = next;
	}

	s->subpartition_head = NULL;
	s->subpartition_tail = NULL;
}

long
measure_activated_swap(const struct i_fn_args *a)
{
	FILE *p;
	char line[256];
	char *word;
	long swap = 0;

	if ((p = aura_popen("%s%s -k", "r", a->os_root, cmd_name(a, "SWAPINFO"))) == NULL)
		return(0);
	while (fgets(line, 255, p) != NULL) {
		if ((word = strtok(line, " \t")) == NULL)
			continue;
		if (strcmp(word, "Device") == 0)
			continue;
		if ((word = strtok(NULL, " \t")) == NULL)
			continue;
		swap += atol(word);
	}
	aura_pclose(p);

	return(swap / 1024);
}

long
measure_activated_swap_from_slice(const struct i_fn_args *a,
    const struct disk *d, const struct slice *s)
{
	FILE *p;
	char *dev, *word;
	char line[256];
	long swap = 0;

	if ((p = aura_popen("%s%s -k", "r", a->os_root, cmd_name(a, "SWAPINFO"))) == NULL)
		return(0);

	asprintf(&dev, "/dev/%ss%d", d->device, s->number);

	while (fgets(line, 255, p) != NULL) {
		if ((word = strtok(line, " \t")) == NULL)
			continue;
		if (strcmp(word, "Device") == 0)
			continue;
		if (strstr(word, dev) != word)
			continue;
		if ((word = strtok(NULL, " \t")) == NULL)
			continue;
		swap += atol(word);
	}
	aura_pclose(p);
	free(dev);

	return(swap / 1024);
}

long
measure_activated_swap_from_disk(const struct i_fn_args *a,
				 const struct disk *d)
{
	struct slice *s;
	long swap = 0;

	for (s = d->slice_head; s != NULL; s = s->next)
		swap += measure_activated_swap_from_slice(a, d, s);

	return(swap);
}

void *
swapoff_all(const struct i_fn_args *a)
{
	FILE *p;

	if ((p = aura_popen("%s%s off; %s%s | %s%s \"^/dev\" | %s%s '{print $1;}' | %s%s %s%s", "r",
		    a->os_root, cmd_name(a, "DUMPON"),
		    a->os_root, cmd_name(a, "SWAPINFO"),
		    a->os_root, cmd_name(a, "GREP"),
		    a->os_root, cmd_name(a, "AWK"),
		    a->os_root, cmd_name(a, "XARGS"),
		    a->os_root, cmd_name(a, "SWAPOFF"))) != NULL)
		aura_pclose(p);

	return(p);
}

void *
remove_all_mappings(const struct i_fn_args *a)
{
	FILE *p;

	if ((p = aura_popen("%s%s -1 /dev/mapper | %s%s -vw control | %s%s -n 1 %s%s luksClose", "r",
		    a->os_root, cmd_name(a, "LS"),
		    a->os_root, cmd_name(a, "GREP"),
		    a->os_root, cmd_name(a, "XARGS"),
		    a->os_root, cmd_name(a, "CRYPTSETUP"))) != NULL)
		aura_pclose(p);

	return(p);
}
