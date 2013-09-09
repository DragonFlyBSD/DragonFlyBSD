 /*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
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

#include <stdio.h>
#include <sys/sysctl.h>

#include "libhammer.h"

static
int
dosysctl(const char *sname, int64_t *value)
{
	size_t len;
	int error;

	len = sizeof(*value);
	error = sysctlbyname(sname, value, &len, NULL, 0);

	return error;
}

int
libhammer_stats_redo(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_redo", value));
}

int
libhammer_stats_undo(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_undo", value));
}

int
libhammer_stats_commits(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_commits", value));
}

int
libhammer_stats_inode_flushes(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_inode_flushes", value));
}

int
libhammer_stats_disk_write(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_disk_write", value));
}

int
libhammer_stats_disk_read(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_disk_read", value));
}

int
libhammer_stats_file_iopsw(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_file_iopsw", value));
}

int
libhammer_stats_file_iopsr(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_file_iopsr", value));
}

int
libhammer_stats_file_write(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_file_write", value));
}

int
libhammer_stats_file_read(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_file_read", value));
}

int
libhammer_stats_record_iterations(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_record_iterations", value));
}

int
libhammer_stats_root_iterations(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_root_iterations", value));
}

int
libhammer_stats_btree_iterations(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_iterations", value));
}

int
libhammer_stats_btree_splits(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_splits", value));
}

int
libhammer_stats_btree_elements(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_elements", value));
}

int
libhammer_stats_btree_deletes(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_deletes", value));
}

int
libhammer_stats_btree_inserts(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_inserts", value));
}

int
libhammer_stats_btree_lookups(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_lookups", value));
}

int
libhammer_stats_btree_searches(int64_t *value)
{
	return (dosysctl("vfs.hammer.stats_btree_searches", value));
}

int
libhammer_btree_stats(struct libhammer_btree_stats *bstats)
{
	int error = 0;

	error = libhammer_stats_btree_elements(&bstats->elements);
	error = libhammer_stats_btree_iterations(&bstats->iterations);
	error = libhammer_stats_btree_splits(&bstats->splits);
	error = libhammer_stats_btree_inserts(&bstats->inserts);
	error = libhammer_stats_btree_deletes(&bstats->deletes);
	error = libhammer_stats_btree_lookups(&bstats->lookups);
	error = libhammer_stats_btree_searches(&bstats->searches);

	return error;
}

int
libhammer_io_stats(struct libhammer_io_stats *iostats)
{
	int error = 0;

	error = libhammer_stats_undo(&iostats->undo);
	error = libhammer_stats_commits(&iostats->commits);
	error = libhammer_stats_inode_flushes(&iostats->inode_flushes);
	error = libhammer_stats_disk_write(&iostats->dev_writes);
	error = libhammer_stats_disk_read(&iostats->dev_reads);
	error = libhammer_stats_file_iopsw(&iostats->file_iop_writes);
	error = libhammer_stats_file_iopsr(&iostats->file_iop_reads);
	error = libhammer_stats_file_write(&iostats->file_writes);
	error = libhammer_stats_file_read(&iostats->file_reads);

	return error;
}
