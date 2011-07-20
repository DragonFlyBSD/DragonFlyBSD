/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

/* Make dm_task opaque */
struct dm_task;

struct dm_info {
	int		exists;
	int		suspended;
	int		live_table;
	int		inactive_table;
	int		read_only;
	int32_t		open_count;
	int32_t		target_count;
	uint32_t	event_nr;
	uint32_t	major;
	uint32_t	minor;
};

struct dm_names {
	const char	*name;
	uint64_t	dev;
	uint32_t	next;
};

struct dm_deps {
	uint32_t	count;
	uint64_t	deps[1];
};

struct dm_versions {
	const char	*name;
	uint32_t	version[3];
	uint32_t	next;
};

/* XXX: dm_task_set_geometry */

enum {
	DM_DEVICE_REMOVE,
	DM_DEVICE_REMOVE_ALL,
	DM_DEVICE_CREATE,
	DM_DEVICE_RELOAD,
	DM_DEVICE_RESUME,
	DM_DEVICE_SUSPEND,
	DM_DEVICE_CLEAR,
	DM_DEVICE_LIST_VERSIONS,
	DM_DEVICE_STATUS,
	DM_DEVICE_TABLE,
	DM_DEVICE_INFO,
	DM_DEVICE_DEPS,
	DM_DEVICE_VERSION,
	DM_DEVICE_TARGET_MSG,
	DM_DEVICE_RENAME,
	DM_DEVICE_LIST
};

/* int level, const char *file, int line, const char *fmt, ... */
typedef void (*dm_error_func_t)(int, const char *, int, const char *, ...);

struct dm_task *dm_task_create(int task_type);
void dm_task_destroy(struct dm_task *dmt);
int dm_task_run(struct dm_task *dmt);
int dm_task_set_name(struct dm_task *dmt, const char *name);
const char *dm_task_get_name(struct dm_task *dmt);
int dm_task_set_newname(struct dm_task *dmt, const char *newname);
int dm_task_set_major(struct dm_task *dmt, int major);
int dm_task_set_minor(struct dm_task *dmt, int minor);
int dm_task_get_minor(struct dm_task *dmt);
int dm_task_set_uuid(struct dm_task *dmt, const char *uuid);
const char *dm_task_get_uuid(struct dm_task *dmt);
int dm_task_add_target(struct dm_task *dmt, uint64_t start, size_t size,
    const char *target, const char *params);
int dm_task_set_sector(struct dm_task *dmt, uint64_t sector);
int dm_task_set_message(struct dm_task *dmt, const char *msg);
int dm_task_set_ro(struct dm_task *dmt);
int dm_task_no_open_count(struct dm_task *dmt);
int dm_task_query_inactive_table(struct dm_task *dmt);
int dm_task_set_read_ahead(struct dm_task *dmt, uint32_t read_ahead);
int dm_task_get_read_ahead(struct dm_task *dmt, uint32_t *read_ahead);
int dm_task_secure_data(struct dm_task *dmt);
int dm_task_get_info(struct dm_task *dmt, struct dm_info *dmi);
int dm_task_get_driver_version(struct dm_task *dmt, char *ver, size_t ver_sz);
struct dm_deps *dm_task_get_deps(struct dm_task *dmt);
struct dm_versions *dm_task_get_versions(struct dm_task *dmt);
struct dm_names *dm_task_get_names(struct dm_task *dmt);
int dm_task_update_nodes(void);
void *dm_get_next_target(struct dm_task *dmt, void *cur, uint64_t *startp,
    uint64_t *lengthp, char **target_type, char **params);
uint32_t dm_get_major(void);
int dm_is_dm_major(uint32_t major);
const char *dm_dir(void);
void dm_udev_set_sync_support(int sync_udev);
int dm_task_set_cookie(struct dm_task *dmt, uint32_t *cookie,
    uint16_t udev_flags);
int dm_udev_wait(uint32_t cookie);
void dm_lib_release(void);
int dm_log_init(dm_error_func_t fn);
int dm_log_init_verbose(int verbose);
int dm_task_set_uid(struct dm_task *dmt, uid_t uid);
int dm_task_set_gid(struct dm_task *dmt, gid_t gid);
int dm_task_set_mode(struct dm_task *dmt, mode_t mode);
int dm_task_no_flush(struct dm_task *dmt);
int dm_task_skip_lockfs(struct dm_task *dmt);
int dm_task_set_geometry(struct dm_task *dmt, const char *cylinders,
    const char *heads, const char *sectors, const char *start);




/*****************************************************************************/
/********************** DragonFly-specific extensions ************************/
/*****************************************************************************/
void *dm_get_next_version(struct dm_task *dmt, void *cur,
    const char **target_type, uint32_t *target_ver);
void *dm_get_next_dep(struct dm_task *dmt, void *cur, uint64_t *dep);
void *dm_get_next_name(struct dm_task *dmt, void *cur, const char **name,
    uint64_t *dev);

