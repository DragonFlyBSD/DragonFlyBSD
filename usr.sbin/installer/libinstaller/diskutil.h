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
 * diskutil.h
 * $Id: diskutil.h,v 1.14 2005/02/07 06:41:42 cpressey Exp $
 */

#include <stdio.h>

#ifndef __DISKUTIL_H_
#define __DISKUTIL_H_

#include "functions.h"

/*** TYPES ***/

struct storage;
struct disk;
struct slice;
struct subpartition;

#define FS_HAMMER	0
#define FS_UFS		1

#ifdef NEEDS_DISKUTIL_STRUCTURE_DEFINITIONS

struct storage {
	struct disk *disk_head;
	struct disk *disk_tail;
	struct disk *selected_disk;
	struct slice *selected_slice;
	long ram;			/* amount of physical memory in MB */
};

struct disk {
	struct disk *next;
	struct disk *prev;
	struct slice *slice_head;
	struct slice *slice_tail;
	char *desc;			/* from whereever we get the best */
	int number;			/* Position in kern.disks */
	char *device;			/* `ad0', `da1', and such */
	char *serno;			/* serial number */
	int cylinders;			/* geometry information */
	int heads;
	int sectors;			/* (sectors per track) */
	long capacity;			/* capacity in megabytes */
	int we_formatted;		/* did we format it ourselves? */
};

struct slice {
	struct disk *parent;
	struct slice *next;
	struct slice *prev;
	struct subpartition *subpartition_head;
	struct subpartition *subpartition_tail;
	char *desc;			/* description (w/sysid string) */
	int number;			/* 1 - 4 (or more?) (from fdisk) */
	unsigned long start;		/* start sector (from fdisk) */
	unsigned long size;		/* size in sectors (from fdisk) */
	int type;			/* sysid of slice (from fdisk) */
	int flags;			/* flags (from fdisk) */
	unsigned long capacity;		/* capacity in megabytes */
};

struct subpartition {
	struct slice *parent;
	struct subpartition *next;
	struct subpartition *prev;
	char letter;			/* 'a' = root partition */
	char *mountpoint;		/* includes leading slash */
	long capacity;			/* in megabytes, -1 = "rest of disk" */
	int encrypted;
	int softupdates;
	long fsize;			/* fragment size */
	long bsize;			/* block size */
	int is_swap;
	int tmpfsbacked;		/* TMPFS Backed */
	int type;			/* FS type (UFS, HAMMER) */
	int pfs;			/* HAMMER pseudo file system */
};

#endif /* NEEDS_DISKUTIL_STRUCTURE_DEFINITIONS */

/*** PROTOTYPES ***/

struct storage		*storage_new(void);
void			 storage_free(struct storage *);
void			 storage_set_memsize(struct storage *, unsigned long);
long			 storage_get_memsize(const struct storage *);
struct disk		*storage_disk_first(const struct storage *);
void			 storage_set_selected_disk(struct storage *, struct disk *);
struct disk		*storage_get_selected_disk(const struct storage *);
void			 storage_set_selected_slice(struct storage *, struct slice *);
struct slice		*storage_get_selected_slice(const struct storage *);
int			 storage_get_tmpfs_status(const char *, struct storage *);

struct disk		*disk_new(struct storage *, const char *);
struct disk		*disk_find(const struct storage *, const char *);
struct disk		*disk_next(const struct disk *);
void			 disks_free(struct storage *);
void			 disk_set_number(struct disk *, const int);
void			 disk_set_desc(struct disk *, const char *);
void			 disk_set_serno(struct disk *, const char *);
int			 disk_get_number(const struct disk *);
const char		*disk_get_desc(const struct disk *);
const char		*disk_get_device_name(const struct disk *);
const char		*disk_get_serno(const struct disk *);
struct slice		*disk_slice_first(const struct disk *);
void			 disk_set_formatted(struct disk *, int);
int			 disk_get_formatted(const struct disk *);
void			 disk_set_geometry(struct disk *, int, int, int);
void			 disk_get_geometry(const struct disk *, int *, int *, int *);

struct slice		*slice_new(struct disk *, int, int, int,
				   unsigned long, unsigned long);
struct slice		*slice_find(const struct disk *, int);
struct slice		*slice_next(const struct slice *);
int			 slice_get_number(const struct slice *);
const char		*slice_get_desc(const struct slice *);
const char		*slice_get_device_name(const struct slice *);
unsigned long		 slice_get_capacity(const struct slice *);
unsigned long		 slice_get_start(const struct slice *);
unsigned long		 slice_get_size(const struct slice *);
int			 slice_get_type(const struct slice *);
int			 slice_get_flags(const struct slice *);
void			 slices_free(struct slice *);
struct subpartition	*slice_subpartition_first(const struct slice *);

struct subpartition	*subpartition_new_hammer(struct slice *, const char *,
						 long, int);
struct subpartition	*subpartition_new_ufs(struct slice *, const char *,
					      long, int, int, long, long, int);
int			 subpartition_count(const struct slice *);
struct subpartition	*subpartition_find(const struct slice *, const char *, ...)
			     __printflike(2, 3);
struct subpartition	*subpartition_of(const struct slice *, const char *, ...)
			     __printflike(2, 3);
struct subpartition	*subpartition_find_capacity(const struct slice *, long);
void		 	 subpartitions_free(struct slice *);
struct subpartition	*subpartition_next(const struct subpartition *);
int 			 subpartition_get_pfs(const struct subpartition *);
const char		*subpartition_get_mountpoint(const struct subpartition *);
const char		*subpartition_get_device_name(const struct subpartition *);
char			 subpartition_get_letter(const struct subpartition *);
unsigned long		 subpartition_get_fsize(const struct subpartition *);
unsigned long		 subpartition_get_bsize(const struct subpartition *);
long			 subpartition_get_capacity(const struct subpartition *);
int			 subpartition_is_encrypted(const struct subpartition *);
int			 subpartition_is_swap(const struct subpartition *);
int			 subpartition_is_softupdated(const struct subpartition *);
int			 subpartition_is_tmpfsbacked(const struct subpartition *);

long			 measure_activated_swap(const struct i_fn_args *);
long			 measure_activated_swap_from_slice(const struct i_fn_args *,
				const struct disk *, const struct slice *);
long			 measure_activated_swap_from_disk(const struct i_fn_args *,
				const struct disk *);
void			*swapoff_all(const struct i_fn_args *);
void			*remove_all_mappings(const struct i_fn_args *);

int			 survey_storage(struct i_fn_args *);

#endif /* !__DISKUTIL_H_ */
