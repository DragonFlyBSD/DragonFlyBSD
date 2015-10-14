/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)kvm_file.c	8.1 (Berkeley) 6/4/93
 * $FreeBSD: src/lib/libkvm/kvm_file.c,v 1.11 2000/02/18 16:39:00 peter Exp $
 */

/*
 * File list interface for kvm.
 */

#include <sys/user.h>	/* MUST BE FIRST */
#include <sys/param.h>
#include <sys/proc.h>
#define _KERNEL
#include <sys/file.h>
#undef _KERNEL
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/kinfo.h>
#include <nlist.h>
#include <kvm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/swap_pager.h>

#include <sys/sysctl.h>

#include <limits.h>
#include <ndbm.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvm_private.h"

#define KREAD(kd, addr, obj) \
	(kvm_read(kd, addr, obj, sizeof(*obj)) != sizeof(*obj))

/* XXX copied from sys/kern/subr_kcore.c */
static void
kcore_make_file(struct kinfo_file *ufile, struct file *kfile,
    pid_t pid, uid_t owner, int n)
{
	bzero(ufile, sizeof(*ufile));
	ufile->f_size = sizeof(*ufile);
	ufile->f_pid = pid;
	ufile->f_uid = owner;

	ufile->f_fd = n;
	ufile->f_file = kfile;
	ufile->f_data = kfile->f_data;
	ufile->f_type = kfile->f_type;
	ufile->f_count = kfile->f_count;
	ufile->f_msgcount = kfile->f_msgcount;
	ufile->f_offset = kfile->f_offset;
	ufile->f_flag = kfile->f_flag;
}

/*
 * Get file structures.
 */
static int
kvm_deadfiles(kvm_t *kd, struct kinfo_proc *kproc, int kproc_cnt, int nfiles)
{
	struct kinfo_file *kinfo_file = (struct kinfo_file *)kd->argspc;
	struct fdnode *fd_files;
	int i, fd_nfiles, found = 0;

	fd_nfiles = NDFILE;
	fd_files = malloc(fd_nfiles * sizeof(struct fdnode));
	if (fd_files == NULL) {
		_kvm_err(kd, kd->program, "alloc fd_files failed");
		return 0;
	}

	for (i = 0; i < kproc_cnt; ++i) {
		const struct kinfo_proc *kp = &kproc[i];
		struct filedesc fdp;
		int n, f;

		if (kp->kp_fd == 0)
			continue;
		if (KREAD(kd, kp->kp_fd, &fdp)) {
			_kvm_err(kd, kd->program, "can't read fdp");
			free(fd_files);
			return 0;
		}
		if (fdp.fd_files == NULL)
			continue;

		if (fdp.fd_nfiles > fd_nfiles) {
			struct fdnode *new_fd_files;

			fd_nfiles = fdp.fd_nfiles;
			new_fd_files =
			    malloc(fd_nfiles * sizeof(struct fdnode));
			free(fd_files);
			if (new_fd_files == NULL) {
				_kvm_err(kd, kd->program,
				    "realloc fd_files failed");
				return 0;
			}
			fd_files = new_fd_files;
		}
		n = fdp.fd_nfiles * sizeof(struct fdnode);

		if (kvm_read(kd, (uintptr_t)fdp.fd_files, fd_files, n) != n) {
			_kvm_err(kd, kd->program, "can't read fd_files");
			free(fd_files);
			return 0;
		}
		for (f = 0; f < fdp.fd_nfiles; ++f) {
			struct file kf;

			if (fd_files[f].fp == NULL)
				continue;
			if (KREAD(kd, (uintptr_t)fd_files[f].fp, &kf)) {
				_kvm_err(kd, kd->program, "can't read file");
				free(fd_files);
				return 0;
			}

			kcore_make_file(kinfo_file, &kf,
			    kp->kp_pid, kp->kp_uid, f);
			kinfo_file++;
			found++;

			if (found == nfiles) {
				size_t size;

				nfiles *= 2;
				size = nfiles * sizeof(struct kinfo_file);

				kd->argspc = _kvm_realloc(kd, kd->argspc, size);
				if (kd->argspc == NULL) {
					free(fd_files);
					return 0;
				}
				kd->arglen = size;

				kinfo_file = (struct kinfo_file *)kd->argspc;
				kinfo_file += found;
			}
		}
	}
	free(fd_files);
	return found;
}

struct kinfo_file *
kvm_getfiles(kvm_t *kd, int op, int arg, int *cnt)
{
	int nfiles;
	size_t size;

	if (kvm_ishost(kd)) {
		int mib[2], st;

		size = 0;
		mib[0] = CTL_KERN;
		mib[1] = KERN_FILE;
		st = sysctl(mib, 2, NULL, &size, NULL, 0);
		if (st == -1) {
			_kvm_syserr(kd, kd->program, "sysctl KERN_FILE failed");
			return NULL;
		}
		if (kd->argspc == NULL)
			kd->argspc = _kvm_malloc(kd, size);
		else if (kd->arglen < size)
			kd->argspc = _kvm_realloc(kd, kd->argspc, size);
		if (kd->argspc == NULL)
			return NULL;
		kd->arglen = size;
		st = sysctl(mib, 2, kd->argspc, &size, NULL, 0);
		if (st == -1 || size % sizeof(struct kinfo_file) != 0) {
			_kvm_syserr(kd, kd->program, "sysctl KERN_FILE failed");
			return NULL;
		}
		nfiles = size / sizeof(struct kinfo_file);
	} else {
		struct kinfo_proc *kproc0, *kproc;
		int kproc_cnt, kproc_len;
		struct nlist nl[2];

		/*
		 * Get all processes and save them in kproc.
		 */
		kproc0 = kvm_getprocs(kd, KERN_PROC_ALL, 0, &kproc_cnt);
		if (kproc0 == NULL)
			return NULL;
		kproc_len = kproc_cnt * sizeof(struct kinfo_proc);
		kproc = malloc(kproc_len);
		if (kproc == NULL) {
			_kvm_syserr(kd, kd->program,
			    "malloc kinfo_proc failed");
			return NULL;
		}
		memcpy(kproc, kproc0, kproc_len);

		/*
		 * Get the # of files
		 */
		memset(nl, 0, sizeof(nl));
		nl[0].n_name = "_nfiles";
		if (kvm_nlist(kd, nl) != 0) {
			_kvm_err(kd, kd->program, "%s: no such symbol",
			    nl[0].n_name);
			free(kproc);
			return NULL;
		}
		if (KREAD(kd, nl[0].n_value, &nfiles)) {
			_kvm_err(kd, kd->program, "can't read nfiles");
			free(kproc);
			return NULL;
		}

		/*
		 * stdio/stderr/stdout are normally duplicated
		 * across all processes.
		 */
		nfiles += (kproc_cnt * 3);
		size = nfiles * sizeof(struct kinfo_file);
		if (kd->argspc == NULL)
			kd->argspc = _kvm_malloc(kd, size);
		else if (kd->arglen < size)
			kd->argspc = _kvm_realloc(kd, kd->argspc, size);
		if (kd->argspc == NULL) {
			free(kproc);
			return NULL;
		}
		kd->arglen = size;

		nfiles = kvm_deadfiles(kd, kproc, kproc_cnt, nfiles);
		free(kproc);

		if (nfiles == 0)
			return NULL;
	}
	*cnt = nfiles;
	return (struct kinfo_file *)(kd->argspc);
}
