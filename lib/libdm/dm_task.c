/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <machine/inttypes.h>
#include <dev/disk/dm/netbsd-dm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <libprop/proplib.h>
#include "libdm.h"

struct dm_task {
	int			task_type;
	int			was_enoent;
	prop_dictionary_t	dict;
	void			*data_buffer;
};

struct dm_cmd {
	int		task_type;
	const char	*dm_cmd;
	uint32_t	cmd_version[3];
};

struct dm_cmd dm_cmds[] = {
	{ DM_DEVICE_REMOVE,		"remove",	{4, 0, 0} },
	{ DM_DEVICE_REMOVE_ALL,		"remove_all",	{4, 0, 0} },
	{ DM_DEVICE_CREATE,		"create",	{4, 0, 0} },
	{ DM_DEVICE_RELOAD,		"reload",	{4, 0, 0} },
	{ DM_DEVICE_RESUME,		"resume",	{4, 0, 0} },
	{ DM_DEVICE_SUSPEND,		"suspend",	{4, 0, 0} },
	{ DM_DEVICE_CLEAR,		"clear",	{4, 0, 0} },
	{ DM_DEVICE_LIST_VERSIONS,	"targets",	{4, 1, 0} },
	{ DM_DEVICE_STATUS,		"status",	{4, 0, 0} },
	{ DM_DEVICE_TABLE,		"table",	{4, 0, 0} },
	{ DM_DEVICE_INFO,		"info",		{4, 0, 0} },
	{ DM_DEVICE_DEPS,		"deps",		{4, 0, 0} },
	{ DM_DEVICE_VERSION,		"version",	{4, 0, 0} },
	{ DM_DEVICE_TARGET_MSG,		"message",	{4, 2, 0} },
	{ DM_DEVICE_RENAME,		"rename",	{4, 0, 0} },
	{ DM_DEVICE_LIST,		"names",	{4, 0, 0} },
	{ 0,				NULL,		{0, 0, 0} }
};

#define _LOG_DEBUG	0
#define _LOG_WARN	5
#define _LOG_ERR	10

static void _stderr_log(int level, const char *file,
    int line, const char *fmt, ...)
{
	const char *prefix;
	__va_list ap;

	switch (level) {
	case _LOG_DEBUG:
		prefix = "debug: ";
		break;
	case _LOG_WARN:
		prefix = "warning: ";
		break;
	case _LOG_ERR:
		prefix = "error: ";
		break;
	default:
		prefix = "";
	}

	fprintf(stderr, "libdm %s:%d: ", file, line);
	fprintf(stderr, "%s", prefix);

	__va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	__va_end(ap);

	fprintf(stderr, "\n");

	return;
}

static dm_error_func_t dm_log = _stderr_log;

struct dm_task *
dm_task_create(int task_type)
{
	struct dm_task *dmt;
	struct dm_cmd *cmd = NULL;
	const char *task_cmd = NULL;
	prop_array_t pa;
	uint32_t flags = DM_EXISTS_FLAG;
	int i;

	for (i = 0; dm_cmds[i].dm_cmd != NULL; i++) {
		if (dm_cmds[i].task_type == task_type) {
			cmd = &dm_cmds[i];
			task_cmd = dm_cmds[i].dm_cmd;
			break;
		}
	}

	if (task_cmd == NULL)
		return NULL;

	if (task_type == DM_DEVICE_TABLE)
		flags |= DM_STATUS_TABLE_FLAG;

	if (task_type == DM_DEVICE_SUSPEND)
		flags |= DM_SUSPEND_FLAG;

	if ((dmt = malloc(sizeof(*dmt))) == NULL)
		return NULL;

	memset(dmt, 0, sizeof(*dmt));

	dmt->task_type = task_type;
	dmt->was_enoent = 0;

	if ((dmt->dict = prop_dictionary_create()) == NULL)
		goto err;

	if ((pa = prop_array_create_with_capacity(3)) == NULL)
		goto err;

	if (!prop_array_add_uint32(pa, cmd->cmd_version[0])) {
		prop_object_release(pa);
		goto err;
	}

	if (!prop_array_add_uint32(pa, cmd->cmd_version[1])) {
		prop_object_release(pa);
		goto err;
	}

	if (!prop_array_add_uint32(pa, cmd->cmd_version[2])) {
		prop_object_release(pa);
		goto err;
	}

	if (!prop_dictionary_set(dmt->dict, DM_IOCTL_VERSION, pa)) {
		prop_object_release(pa);
		goto err;
	}

	prop_object_release(pa);

	if (!prop_dictionary_set_cstring(dmt->dict, DM_IOCTL_COMMAND,
	    task_cmd))
		goto err;

	if (!prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags))
		goto err;

	if ((pa = prop_array_create_with_capacity(5)) == NULL)
		goto err;

	if (!prop_dictionary_set(dmt->dict, DM_IOCTL_CMD_DATA, pa)) {
		prop_object_release(pa);
		goto err;
	}

	prop_object_release(pa);

	return dmt;
	/* NOT REACHED */

err:
	if (dmt->dict != NULL)
		prop_object_release(dmt->dict);
	if (dmt)
		free(dmt);

	return NULL;
}


void
dm_task_destroy(struct dm_task *dmt)
{
	if (dmt) {
		if (dmt->data_buffer)
			free(dmt->data_buffer);

		if (dmt->dict) {
			prop_object_release(dmt->dict);
			dmt->dict = NULL;
		}

		free(dmt);
	}
}

int
dm_task_run(struct dm_task *dmt)
{
	struct dm_task *dmt_internal = NULL;
	prop_dictionary_t ret_pd = NULL;
	prop_array_t pa;
	int error;
	int fd;
	int need_unroll = 0;

	if ((fd = open("/dev/mapper/control", O_RDWR)) < -1)
		goto err;

	pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA);
	if ((dmt->task_type == DM_DEVICE_CREATE) && (pa != NULL) &&
	    (prop_array_count(pa) > 0)) {
		/*
		 * Magic to separate a combined DM_DEVICE_CREATE+RELOAD int
		 * a DM_DEVICE_CREATE and a RELOAD with target table.
		 */

		if ((dmt_internal = dm_task_create(DM_DEVICE_CREATE)) == NULL)
			goto err;
		if (!dm_task_set_name(dmt_internal, dm_task_get_name(dmt)))
			goto err;
		if (!dm_task_set_uuid(dmt_internal, dm_task_get_uuid(dmt)))
			goto err;
		if (!dm_task_run(dmt_internal))
			goto err;
		dm_task_destroy(dmt_internal);
		dmt_internal = NULL;

		if (!prop_dictionary_set_cstring_nocopy(dmt->dict,
		    DM_IOCTL_COMMAND, "reload"))
			goto unroll;
		dmt->task_type = DM_DEVICE_RELOAD;
		if ((error = prop_dictionary_sendrecv_ioctl(dmt->dict, fd,
		    NETBSD_DM_IOCTL, &ret_pd)) != 0) {
			dm_log(_LOG_ERR, __FILE__, __LINE__, "ioctl failed: %d",
			    error);
			goto unroll;
		}

		if (!prop_dictionary_set_cstring_nocopy(dmt->dict,
		    DM_IOCTL_COMMAND, "resume"))
			goto unroll;
		dmt->task_type = DM_DEVICE_RESUME;
		/* Remove superfluous stuff */
		prop_dictionary_remove(dmt->dict, DM_IOCTL_CMD_DATA);

		need_unroll = 1;
	}

	if ((error = prop_dictionary_sendrecv_ioctl(dmt->dict, fd,
	    NETBSD_DM_IOCTL, &ret_pd)) != 0) {
		if (((error == ENOENT) &&
		    ((dmt->task_type == DM_DEVICE_INFO) ||
		    (dmt->task_type == DM_DEVICE_STATUS)))) {
			dmt->was_enoent = 1;
			ret_pd = NULL;
		} else {
			dm_log(_LOG_ERR, __FILE__, __LINE__, "ioctl failed: %d",
			    error);
			if (need_unroll)
				goto unroll;
			else
				goto err;
		}
	}

	if (ret_pd)
		prop_object_retain(ret_pd);

	prop_object_release(dmt->dict);
	dmt->dict = ret_pd;

	return 1;
	/* NOT REACHED */

unroll:
	prop_dictionary_remove(dmt->dict, DM_IOCTL_CMD_DATA);

	if (!prop_dictionary_set_cstring_nocopy(dmt->dict, DM_IOCTL_COMMAND,
	    "remove")) {
		dm_log(_LOG_ERR, __FILE__, __LINE__, "couldn't unroll changes "
		    "in dm_task_run");
		goto err;
	}

	if ((error = prop_dictionary_sendrecv_ioctl(dmt->dict, fd,
	    NETBSD_DM_IOCTL, &ret_pd)) != 0) {
		dm_log(_LOG_ERR, __FILE__, __LINE__, "ioctl failed: %d",
		    error);
		goto unroll;
	}
	dmt->task_type = DM_DEVICE_REMOVE;
	dm_task_run(dmt);

err:
	if (fd >= 0)
		close(fd);

	if (dmt_internal)
		dm_task_destroy(dmt_internal);

	return 0;
}

int
dm_task_set_name(struct dm_task *dmt, const char *name)
{
	return prop_dictionary_set_cstring(dmt->dict, DM_IOCTL_NAME,
	    __DECONST(char *, name));
}


const char *
dm_task_get_name(struct dm_task *dmt)
{
	const char *name = NULL;

	prop_dictionary_get_cstring_nocopy(dmt->dict, DM_IOCTL_NAME, &name);

	return name;
}

int
dm_task_set_newname(struct dm_task *dmt, const char *newname)
{
	return prop_dictionary_set_cstring(dmt->dict, DM_DEV_NEWNAME,
	    __DECONST(char *, newname));
}

int
dm_task_set_major(struct dm_task *dmt __unused, int major __unused)
{
	return 1;
}

int
dm_task_set_minor(struct dm_task *dmt, int minor)
{
	return prop_dictionary_set_int32(dmt->dict, DM_IOCTL_MINOR, minor);
}

int
dm_task_get_minor(struct dm_task *dmt)
{
	int minor = 0;

	minor = prop_dictionary_get_int32(dmt->dict, DM_IOCTL_MINOR, &minor);

	return minor;
}

int
dm_task_set_uuid(struct dm_task *dmt, const char *uuid)
{
	return prop_dictionary_set_cstring(dmt->dict, DM_IOCTL_UUID,
	    __DECONST(char *,uuid));
}

const char *
dm_task_get_uuid(struct dm_task *dmt)
{
	const char *uuid = NULL;

	prop_dictionary_get_cstring_nocopy(dmt->dict, DM_IOCTL_UUID, &uuid);

	return uuid;
}

int
dm_task_add_target(struct dm_task *dmt, uint64_t start, size_t size,
    const char *target, const char *params)
{
	prop_dictionary_t target_dict = NULL;
	prop_array_t pa = NULL;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return 0;

	if ((target_dict = prop_dictionary_create()) == NULL)
		return 0;

	if (!prop_dictionary_set_uint64(target_dict, DM_TABLE_START, start))
		goto err;

	if (!prop_dictionary_set_uint64(target_dict, DM_TABLE_LENGTH, size))
		goto err;

	if (!prop_dictionary_set_cstring(target_dict, DM_TABLE_TYPE, target))
		goto err;

	if (!prop_dictionary_set_cstring(target_dict, DM_TABLE_PARAMS, params))
		goto err;

	if (!prop_array_add(pa, target_dict))
		goto err;

	prop_object_release(target_dict);

	return 1;
	/* NOT REACHED */

err:
	prop_object_release(target_dict);
	return 0;
}

int
dm_task_set_sector(struct dm_task *dmt, uint64_t sector)
{
	return prop_dictionary_set_uint64(dmt->dict, DM_MESSAGE_SECTOR,
	    sector);
}

int
dm_task_set_message(struct dm_task *dmt, const char *msg)
{
	return prop_dictionary_set_cstring(dmt->dict, DM_MESSAGE_STR, msg);
}

int
dm_task_set_ro(struct dm_task *dmt)
{
	uint32_t flags = 0;

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS, &flags);
	flags |= DM_READONLY_FLAG;

	return prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags);
}

int
dm_task_no_open_count(struct dm_task *dmt __unused)
{
	/*
	 * nothing else needed, since we don't have performance problems when
	 * getting the open count.
	 */
	return 1;
}

int
dm_task_query_inactive_table(struct dm_task *dmt)
{
	uint32_t flags = 0;

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS, &flags);
	flags |= DM_QUERY_INACTIVE_TABLE_FLAG;

	return prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags);
}

int
dm_task_set_read_ahead(struct dm_task *dmt __unused,
    uint32_t read_ahead __unused)
{
	/* We don't support readahead */
	return 1;
}

int
dm_task_get_read_ahead(struct dm_task *dmt __unused, uint32_t *read_ahead)
{
	*read_ahead = 0;

	return 1;
}

int
dm_task_secure_data(struct dm_task *dmt)
{
	/* XXX: needs kernel support */
	uint32_t flags = 0;

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS, &flags);
	flags |= DM_SECURE_DATA_FLAG;

	return prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags);
}

int
dm_task_get_info(struct dm_task *dmt, struct dm_info *dmi)
{
	uint32_t flags = 0;

	memset(dmi, 0, sizeof(struct dm_info));

	/* Hack due to the way Linux dm works */
	if (dmt->was_enoent) {
		dmi->exists = 0;
		return 1;
		/* NOT REACHED */
	}

	if (!prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS,
	    &flags))
		return 0;

	prop_dictionary_get_int32(dmt->dict, DM_IOCTL_OPEN, &dmi->open_count);

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_TARGET_COUNT,
	    &dmi->target_count);

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_EVENT, &dmi->event_nr);

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_MINOR, &dmi->minor);

	dmi->major = dm_get_major();

	dmi->read_only = (flags & DM_READONLY_FLAG);
	dmi->exists = (flags & DM_EXISTS_FLAG);
	dmi->suspended = (flags & DM_SUSPEND_FLAG);
	dmi->live_table = (flags & DM_ACTIVE_PRESENT_FLAG);
	dmi->inactive_table = (flags & DM_INACTIVE_PRESENT_FLAG);

	return 1;
}

int
dm_task_get_driver_version(struct dm_task *dmt, char *ver, size_t ver_sz)
{
	prop_array_t pa_ver;
	uint32_t maj = 0, min = 0, patch = 0;

	if ((pa_ver = prop_dictionary_get(dmt->dict, DM_IOCTL_VERSION)) == NULL)
		return 0;

	if (!prop_array_get_uint32(pa_ver, 0, &maj))
		return 0;

	if (!prop_array_get_uint32(pa_ver, 1, &min))
		return 0;

	if (!prop_array_get_uint32(pa_ver, 2, &patch))
		return 0;

	snprintf(ver, ver_sz, "%u.%u.%u", maj, min, patch);

	return 1;
}

struct dm_deps *
dm_task_get_deps(struct dm_task *dmt)
{
	prop_object_iterator_t iter;
	prop_array_t pa;
	prop_object_t po;
	struct dm_deps *deps;

	unsigned int count;
	int i;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (dmt->data_buffer != NULL)
		free(dmt->data_buffer);

	if ((dmt->data_buffer = malloc(sizeof(struct dm_deps) +
	    (count * sizeof(uint64_t)))) == NULL)
		return NULL;

	if ((iter = prop_array_iterator(pa)) == NULL)
		return NULL;

	deps = (struct dm_deps *)dmt->data_buffer;
	memset(deps, 0, sizeof(struct dm_deps) + (count * sizeof(uint64_t)));
	i = 0;
	while ((po = prop_object_iterator_next(iter)) != NULL)
		deps->deps[i++] = prop_number_unsigned_integer_value(po);

	deps->count = (uint32_t)count;

	prop_object_iterator_release(iter);

	return deps;
}

struct dm_versions *
dm_task_get_versions(struct dm_task *dmt)
{
	prop_object_iterator_t iter;
	prop_dictionary_t target_dict;
	prop_array_t pa, pa_ver;
	struct dm_versions *vers;

	unsigned int count;
	int i, j;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (dmt->data_buffer != NULL)
		free(dmt->data_buffer);

	if ((dmt->data_buffer = malloc(sizeof(struct dm_versions) * count))
	    == NULL)
		return NULL;

	if ((iter = prop_array_iterator(pa)) == NULL)
		return NULL;

	vers = (struct dm_versions *)dmt->data_buffer;
	memset(vers, 0, sizeof(struct dm_versions) * count);
	i = 0;
	while ((target_dict = prop_object_iterator_next(iter)) != NULL) {
		vers[i].next = sizeof(struct dm_versions);
		prop_dictionary_get_cstring_nocopy(target_dict,
		    DM_TARGETS_NAME, &vers[i].name);

		pa_ver = prop_dictionary_get(target_dict, DM_TARGETS_VERSION);
		for (j = 0; j < 3; j++)
			prop_array_get_uint32(pa_ver, j, &vers[i].version[j]);

		++i;
	}

	/* Finish the array */
	vers[i-1].next = 0;

	prop_object_iterator_release(iter);

	return (struct dm_versions *)dmt->data_buffer;
}

struct dm_names *
dm_task_get_names(struct dm_task *dmt)
{
	prop_object_iterator_t iter;
	prop_dictionary_t devs_dict;
	prop_array_t pa;
	struct dm_names *names;

	unsigned int count;
	int i;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (dmt->data_buffer != NULL)
		free(dmt->data_buffer);

	if ((dmt->data_buffer = malloc(sizeof(struct dm_names) * count))
	    == NULL)
		return NULL;

	if ((iter = prop_array_iterator(pa)) == NULL)
		return NULL;

	names = (struct dm_names *)dmt->data_buffer;
	memset(names, 0, sizeof(struct dm_names) * count);
	i = 0;
	while ((devs_dict = prop_object_iterator_next(iter)) != NULL) {
		names[i].next = sizeof(struct dm_names);

		prop_dictionary_get_cstring_nocopy(devs_dict,
		    DM_DEV_NAME, &names[i].name);

		prop_dictionary_get_uint64(devs_dict, DM_DEV_DEV,
		    &names[i].dev);

		++i;
	}

	/* Finish the array */
	names[i-1].next = 0;

	prop_object_iterator_release(iter);

	return (struct dm_names *)dmt->data_buffer;
}

int
dm_task_update_nodes(void)
{

	/* nothing else needed */
	return 1;
}

void *
dm_get_next_target(struct dm_task *dmt, void *cur, uint64_t *startp,
    uint64_t *lengthp, char **target_type, char **params)
{
	prop_object_iterator_t  iter;
	prop_dictionary_t target_dict;
	prop_array_t pa;
	uint64_t ulength;
	unsigned int count;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (cur == NULL) {
		if ((iter = prop_array_iterator(pa)) == NULL)
			return NULL;
	} else {
		iter = (prop_object_iterator_t)cur;
	}

	/* Get the next target dict */
	if ((target_dict = prop_object_iterator_next(iter)) == NULL) {
		/* If there are no more target dicts, release the iterator */
		goto err;
	}

	if (!prop_dictionary_get_cstring_nocopy(target_dict, DM_TABLE_TYPE,
	    (const char **)target_type))
		goto err;

	/*
	 * Ugly __DECONST and (const char **) casts due to the linux prototype
	 * of this function.
	 */
	*params = __DECONST(char *, "");
	prop_dictionary_get_cstring_nocopy(target_dict, DM_TABLE_PARAMS,
	    (const char **)params);

	if (!prop_dictionary_get_uint64(target_dict, DM_TABLE_START, startp))
		goto err;

	if (!prop_dictionary_get_uint64(target_dict, DM_TABLE_LENGTH, &ulength))
		goto err;

	*lengthp = (size_t)ulength;

	/* If we are at the last element, make sure we return NULL */
	if (target_dict == prop_array_get(pa, count-1))
		goto err;

	return (void *)iter;
	/* NOT REACHED */

err:
	if (iter != NULL)
		prop_object_iterator_release(iter);

	return NULL;
}

uint32_t
dm_get_major(void)
{
	struct stat sb;

	if (stat("/dev/mapper/control", &sb) < 0)
		return 0;

	return (uint32_t)major(sb.st_dev);
}

int
dm_is_dm_major(uint32_t major)
{
	return (major == dm_get_major());
}

const char *
dm_dir(void)
{
	return "/dev/mapper";
}

void
dm_udev_set_sync_support(int sync_udev __unused)
{
	return;
}

int
dm_task_set_cookie(struct dm_task *dmt __unused, uint32_t *cookie __unused,
    uint16_t udev_flags __unused)
{
	return 1;
}

int
dm_udev_wait(uint32_t cookie __unused)
{
	return 1;
}

void
dm_lib_release(void)
{
	return;
}

int
dm_log_init(dm_error_func_t fn)
{
	if (fn)
		dm_log = fn;
	return 1;
}

int
dm_log_init_verbose(int verbose __unused)
{
	return 1;
}

/* XXX: unused in kernel */
int
dm_task_set_uid(struct dm_task *dmt, uid_t uid)
{
	return prop_dictionary_set_uint32(dmt->dict, DM_DEV_UID,
	    (uint32_t)uid);
}

int
dm_task_set_gid(struct dm_task *dmt, gid_t gid)
{
	return prop_dictionary_set_uint32(dmt->dict, DM_DEV_GID,
	    (uint32_t)gid);
}

int
dm_task_set_mode(struct dm_task *dmt, mode_t mode)
{
	return prop_dictionary_set_uint32(dmt->dict, DM_DEV_MODE,
	    (uint32_t)mode);
}

int
dm_task_no_flush(struct dm_task *dmt __unused)
{
	uint32_t flags = 0;

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS, &flags);
	flags |= DM_NOFLUSH_FLAG;

	return prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags);
}

int
dm_task_skip_lockfs(struct dm_task *dmt __unused)
{
	uint32_t flags = 0;

	prop_dictionary_get_uint32(dmt->dict, DM_IOCTL_FLAGS, &flags);
	flags |= DM_SKIP_LOCKFS_FLAG;

	return prop_dictionary_set_uint32(dmt->dict, DM_IOCTL_FLAGS, flags);
}

int dm_task_set_geometry(struct dm_task *dmt __unused,
    const char *cylinders __unused, const char *heads __unused,
    const char *sectors __unused, const char *start __unused)
{
	return 1;
}

/*****************************************************************************/
/********************** DragonFly-specific extensions ************************/
/*****************************************************************************/
void *
dm_get_next_version(struct dm_task *dmt, void *cur, const char **target_type,
    uint32_t *target_ver)
{
	prop_object_iterator_t iter;
	prop_dictionary_t target_dict;
	prop_array_t pa, pa_ver;
	unsigned int count;
	int j;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (cur == NULL) {
		if ((iter = prop_array_iterator(pa)) == NULL)
			return NULL;
	} else {
		iter = (prop_object_iterator_t)cur;
	}

	/* Get the next target dict */
	if ((target_dict = prop_object_iterator_next(iter)) == NULL) {
		/* If there are no more target dicts, release the iterator */
		goto err;
	}

	if (!prop_dictionary_get_cstring_nocopy(target_dict, DM_TARGETS_NAME,
	    target_type))
		goto err;

	if ((pa_ver = prop_dictionary_get(target_dict, DM_TARGETS_VERSION))
	    == NULL)
		goto err;

	for (j = 0; j < 3; j++) {
		if (!prop_array_get_uint32(pa_ver, j, &target_ver[j]))
			goto err;
	}

	/* If we are at the last element, make sure we return NULL */
	if (target_dict == prop_array_get(pa, count-1))
		goto err;

	return (void *)iter;
	/* NOT REACHED */

err:
	if (iter != NULL)
		prop_object_iterator_release(iter);

	return NULL;
}

void *
dm_get_next_dep(struct dm_task *dmt, void *cur, uint64_t *dep)
{
	prop_object_iterator_t iter;
	prop_object_t po;
	prop_array_t pa;
	unsigned int count;

	*dep = 0;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);

	if (cur == NULL) {
		if ((iter = prop_array_iterator(pa)) == NULL)
			return NULL;
	} else {
		iter = (prop_object_iterator_t)cur;
	}

	/* Get the next target dict */
	if ((po = prop_object_iterator_next(iter)) == NULL) {
		/* If there are no more target dicts, release the iterator */
		goto err;
	}

	*dep = prop_number_unsigned_integer_value(po);

	/* If we are at the last element, make sure we return NULL */
	if (po == prop_array_get(pa, count-1))
		goto err;

	return (void *)iter;
	/* NOT REACHED */

err:
	if (iter != NULL)
		prop_object_iterator_release(iter);

	return NULL;
}

void *
dm_get_next_name(struct dm_task *dmt, void *cur, const char **name,
    uint64_t *dev)
{
	prop_object_iterator_t iter;
	prop_dictionary_t devs_dict;
	prop_array_t pa;
	unsigned int count;

	if ((pa = prop_dictionary_get(dmt->dict, DM_IOCTL_CMD_DATA)) == NULL)
		return NULL;

	count = prop_array_count(pa);


	if (cur == NULL) {
		if ((iter = prop_array_iterator(pa)) == NULL)
			return NULL;
	} else {
		iter = (prop_object_iterator_t)cur;
	}

	/* Get the next dev dict */
	if ((devs_dict = prop_object_iterator_next(iter)) == NULL) {
		/* If there are no more dev dicts, release the iterator */
		goto err;
	}

	if (!prop_dictionary_get_cstring_nocopy(devs_dict, DM_DEV_NAME, name))
		goto err;

	if (!prop_dictionary_get_uint64(devs_dict, DM_DEV_DEV, dev))
		goto err;

	/* If we are at the last element, make sure we return NULL */
	if (devs_dict == prop_array_get(pa, count-1))
		goto err;

	return (void *)iter;
	/* NOT REACHED */

err:
	if (iter != NULL)
		prop_object_iterator_release(iter);

	return NULL;

}
