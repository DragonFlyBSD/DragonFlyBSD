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
#include <sys/stat.h>
#include <devattr.h>
#include <errno.h>
#include "fsid.h"

static struct fs_type fs_types[] = {
	{ "HAMMER",	hammer_probe,	hammer_volname	},
	{ "UFS",	ufs_probe,	ufs_volname	},
	{ NULL,		NULL,		NULL		}
};


static struct fsid_head fsid_list =
		TAILQ_HEAD_INITIALIZER(fsid_list);

static int
fsid_alias_exists(const char *dev)
{
	struct fsid_entry *fsid;
	int exists = 0;

	if (TAILQ_EMPTY(&fsid_list))
		return 0;

	TAILQ_FOREACH(fsid, &fsid_list, link) {
		if (strcmp(fsid->dev_path, dev) == 0) {
			exists = 1;
			break;
		}
	}

	return exists;
}

static int
fsid_check_create_alias(const char *dev)
{
	struct fsid_entry *fsid;
	char full_path[MAXPATHLEN];
	char link_path[MAXPATHLEN];
	char *volname;
	int i;

	if (fsid_alias_exists(dev))
		return EEXIST;

	sprintf(full_path, "/dev/%s", dev);
	for (i = 0; fs_types[i].fs_name != NULL; i++) {
		volname = fs_types[i].fs_volname(full_path);
		if (volname == NULL)
			continue;

		printf("Volume name for %s is %s\n", dev, volname);
		fsid = malloc(sizeof(struct fsid_entry));
		if (fsid == NULL)
			return ENOMEM;
#if 1
		sprintf(link_path, "/dev/vol-by-name/%s", volname);

		fsid->dev_path = strdup(dev);
		fsid->link_path = strdup(link_path);
		if ((fsid->dev_path == NULL) || (fsid->link_path == NULL)) {
			free(fsid);
			return ENOMEM;
		}

		mkdir("/dev/vol-by-name", 0755);
		symlink(full_path, link_path);

		TAILQ_INSERT_TAIL(&fsid_list, fsid, link);
#endif
		break;
	}

	return 0;
}

static int
fsid_check_remove_alias(const char *dev)
{
	struct fsid_entry *fsid, *fsid2;

	if (!fsid_alias_exists(dev))
		return 0;

	TAILQ_FOREACH_MUTABLE(fsid, &fsid_list, link, fsid2) {
		if (strcmp(fsid->dev_path, dev) != 0)
			continue;

		TAILQ_REMOVE(&fsid_list, fsid, link);

		unlink(fsid->link_path);

		free(fsid->dev_path);
		free(fsid->link_path);
		free(fsid);
	}

	return 0;
}

static
void
usage(void)
{
	fprintf(stderr, "usage: fsid [-d]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct udev *udev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_le, *udev_le_first;
	struct udev_monitor *udev_monitor;
	struct udev_device *udev_dev;
	int ch;
#if 0
	prop_dictionary_t *dict;
#endif
	int ret, daemon_flag = 0;
	const char *prop, *dev_path;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch(ch) {
		case 'd':
			daemon_flag = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;

	udev = udev_new();
	if (udev == NULL)
		err(1, "udev_new");

	udev_enum = udev_enumerate_new(udev);
	if (udev_enum == NULL)
		err(1, "udev_enumerate_new");

	ret = udev_enumerate_add_match_property(udev_enum, "subsystem", "disk");
	if (ret != 0)
		err(1, "udev_enumerate_add_match_property, out, ret=%d\n", ret);
#if 1
	ret = udev_enumerate_add_match_property(udev_enum, "alias", "0");
	if (ret != 0)
		err(1, "udev_enumerate_add_match_property, out, ret=%d\n", ret);

	ret = udev_enumerate_add_nomatch_property(udev_enum, "disk-type", "memory");
	if (ret != 0)
		err(1, "udev_enumerate_add_match_property, out, ret=%d\n", ret);
#endif

	ret = udev_enumerate_scan_devices(udev_enum);
	if (ret != 0)
		err(1, "udev_enumerate_scan_device ret = %d", ret);

	udev_le_first = udev_enumerate_get_list_entry(udev_enum);
	if (udev_le_first == NULL)
		err(1, "udev_enumerate_get_list_entry error");

	udev_list_entry_foreach(udev_le, udev_le_first) {
		udev_dev = udev_list_entry_get_device(udev_le);
		dev_path = udev_device_get_devnode(udev_dev);
#if 0
		dict = udev_device_get_dictionary(udev_dev);
		printf("xml of new device: %s\n", prop_dictionary_externalize(dict));
#endif
		fsid_check_create_alias(dev_path);
	}

	udev_enumerate_unref(udev_enum);

	if (daemon_flag) {
#if 0
		if (daemon(0, 0) == -1)
			err(1, "daemon");
#endif

		udev_monitor = udev_monitor_new(udev);
		ret = udev_monitor_filter_add_match_property(udev_monitor, "subsystem", "disk");
		if (ret != 0)
			err(1, "udev_monitor_filter_add_match_property, out, ret=%d\n", ret);

		ret = udev_monitor_filter_add_match_property(udev_monitor, "alias", "0");
		if (ret != 0)
			err(1, "udev_monitor_filter_add_match_property, out, ret=%d\n", ret);

		ret = udev_monitor_filter_add_nomatch_property(udev_monitor, "disk-type", "memory");
		if (ret != 0)
			err(1, "udev_monitor_filter_add_nomatch_property, out, ret=%d\n", ret);

		ret = udev_monitor_enable_receiving(udev_monitor);
		if (ret != 0)
			err(1, "udev_monitor_enable_receiving ret = %d", ret);

		while ((udev_dev = udev_monitor_receive_device(udev_monitor))) {
			if (udev_dev == NULL)
				err(1, "udev_monitor_receive_device failed");

			dev_path = udev_device_get_devnode(udev_dev);
			prop = udev_device_get_action(udev_dev);

			if (strcmp(prop, "attach") == 0)
				fsid_check_create_alias(dev_path);
			else if (strcmp(prop, "detach") == 0)
				fsid_check_remove_alias(dev_path);
		}

		udev_monitor_unref(udev_monitor);
	}

	udev_unref(udev);

	return 0;
}

