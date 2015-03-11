/*
 * DragonFly specific device routines are added to this file.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/sysctl.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <devattr.h>


#define LVM_FAILURE -1


int
dragonfly_check_dev(int major, const char *path)
{
	struct udev *udev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_le, *udev_le_first;
	struct udev_monitor *udev_monitor;
	struct udev_device *udev_dev;
	struct stat sb;
	const char *subsystem;
	const char *driver;
	const char *type;
	int ret, result;

	result = LVM_FAILURE;

	/*
	 * We do the stat & devname dance to get around paths that are
	 * symlinks. udev will only find devices by their real name or
	 * devfs alias.
	 */
	stat(path, &sb);
	path = devname(sb.st_rdev, S_IFCHR);

	if (!strncmp(path, "/dev/", strlen("/dev/"))) {
		path += strlen("/dev/");
	}

	udev = udev_new();
	if (udev == NULL) {
		fprintf(stderr, "udev_new failed! Need udevd running.\n");
		return LVM_FAILURE;
	}

	udev_enum = udev_enumerate_new(udev);
	if (udev_enum == NULL) {
		fprintf(stderr, "udev_enumerate_new failed!\n");
		goto out2;
	}

	ret = udev_enumerate_add_match_expr(udev_enum, "name", __DECONST(char *, path));
	if (ret != 0) {
		fprintf(stderr, "udev_enumerate_add_match_expr failed!\n");
		goto out;
	}

	ret = udev_enumerate_scan_devices(udev_enum);
	if (ret != 0) {
		fprintf(stderr, "udev_enumerate_scan_devices failed!\n");
		goto out;
	}

	udev_le = udev_enumerate_get_list_entry(udev_enum);
	if (udev_le == NULL) {
#if 0
		fprintf(stderr, "udev_enumerate_get_list_entry failed for %s!\n", path);
#endif
		goto out;
	}

	udev_dev = udev_list_entry_get_device(udev_le);
	if (udev_dev == NULL) {
		fprintf(stderr, "udev_list_entry_get_device failed!\n");
		goto out;
	}

	subsystem = udev_device_get_subsystem(udev_dev);
	driver = udev_device_get_driver(udev_dev);
	type = udev_device_get_property_value(udev_dev, "disk-type");

	/* If it's neither a disk driver nor a raid driver, stop here */
	if ((subsystem == NULL) ||
	    ((strcmp(subsystem, "disk") != 0) &&
	    (strcmp(subsystem, "raid") != 0))) {
		goto outdev;
	}

	/* We don't like malloc disks */
	if (driver && (strcmp(driver, "md") == 0)) {
		goto outdev;
	}

	/* Some disk-type checks... */
	if (type && (strcmp(type, "optical") == 0)) {
		goto outdev;
	}

	/* Some disk-type checks... */
	if (type && (strcmp(type, "floppy") == 0)) {
		goto outdev;
	}

	/* Some disk-type checks... */
	if (type && (strcmp(type, "tape") == 0)) {
		goto outdev;
	}

	/* Some disk-type checks... */
	if (type && (strcmp(type, "memory") == 0)) {
		goto outdev;
	}

	result = 0;

outdev:
	udev_device_unref(udev_dev);
out:
	udev_enumerate_unref(udev_enum);
out2:
	udev_unref(udev);
	return result;
}

/*
udev_enumerate_get_list_entry failed for bpf4 (/dev/bpf4)!
udev_enumerate_get_list_entry failed for log (/dev/log)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001 (/dev/serno/00000000000000000001)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0 (/dev/serno/00000000000000000001.s0)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0a (/dev/serno/00000000000000000001.s0a)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0b (/dev/serno/00000000000000000001.s0b)!
udev_enumerate_get_list_entry failed for serno/01000000000000000001 (/dev/serno/01000000000000000001)!
udev_enumerate_get_list_entry failed for sga (/dev/sga)!
udev_enumerate_get_list_entry failed for sgb (/dev/sgb)!
udev_enumerate_get_list_entry failed for bpf4 (/dev/bpf4)!
udev_enumerate_get_list_entry failed for log (/dev/log)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001 (/dev/serno/00000000000000000001)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0 (/dev/serno/00000000000000000001.s0)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0a (/dev/serno/00000000000000000001.s0a)!
udev_enumerate_get_list_entry failed for serno/00000000000000000001.s0b (/dev/serno/00000000000000000001.s0b)!
udev_enumerate_get_list_entry failed for serno/01000000000000000001 (/dev/serno/01000000000000000001)!
udev_enumerate_get_list_entry failed for sga (/dev/sga)!
udev_enumerate_get_list_entry failed for sgb (/dev/sgb)!
*/
