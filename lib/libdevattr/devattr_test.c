#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include "devattr.h"

int main(void)
{
	struct udev *udev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_le, *udev_le_first;
	struct udev_monitor *udev_monitor;
	struct udev_device *udev_dev;
	prop_array_t pa;
	prop_dictionary_t dict;
	int ret;

	printf("1\n");
	udev = udev_new();
	if(udev == NULL) {
		perror("udev_new");
		exit(1);
	}
	printf("2\n");
	udev_enum = udev_enumerate_new(udev);
	if(udev == NULL) {
		perror("udev_enumerate_new");
		exit(1);
	}
	printf("3\n");

	ret = udev_enumerate_add_match_expr(udev_enum, "name", "da*");
	if (ret != 0)
		err(1, "udev_enumerate_add_match_expr, out, ret=%d\n", ret);

	ret = udev_enumerate_add_match_regex(udev_enum, "name", "ad.*");
	if (ret != 0)
		err(1, "udev_enumerate_add_match_regex, out, ret=%d\n", ret);

	ret = udev_enumerate_scan_devices(udev_enum);
	printf("4\n");
	if (ret != 0) {
		printf("4 out...\n");
		err(1, "udev_enumerate_scan_device ret = %d", ret);
	}

	printf("5\n");
	udev_le_first = udev_enumerate_get_list_entry(udev_enum);
	printf("6\n");
	if (udev_le_first == NULL)
		err(1, "udev_enumerate_get_list_entry error");

	printf("7\n");
	pa = udev_enumerate_get_array(udev_enum);
	printf("8\n");
	prop_array_externalize_to_file(pa, "array_out.xml");
	printf("9\n");
	udev_list_entry_foreach(udev_le, udev_le_first) {
		dict = udev_list_entry_get_dictionary(udev_le);
		printf("xml: %s\n\n\n", prop_dictionary_externalize(dict));
	}

	udev_enumerate_unref(udev_enum);

	udev_monitor = udev_monitor_new(udev);
#if 1
	ret = udev_monitor_filter_add_match_regex(udev_monitor, "name", "vn.*");
	if (ret != 0)
		err(1, "udev_monitor_filter_add_match_expr ret = %d", ret);
#endif
#if 0
	ret = udev_monitor_filter_add_nomatch_expr(udev_monitor, "name", "vn*");
	if (ret != 0)
		err(1, "udev_monitor_filter_add_match_expr2 ret = %d", ret);
#endif
	ret = udev_monitor_enable_receiving(udev_monitor);
	if (ret != 0)
		err(1, "udev_monitor_enable_receiving ret = %d", ret);

	printf("meeh\n");
	while ((udev_dev = udev_monitor_receive_device(udev_monitor))) {
		printf("mooh\n");
		if (udev_dev == NULL)
			err(1, "udev_monitor_receive_device failed");

		printf("foo\n");
		dict = udev_device_get_dictionary(udev_dev);
		printf("xml of new device: %s\n", prop_dictionary_externalize(dict));
	}
	udev_monitor_unref(udev_monitor);
	udev_unref(udev);
	return 0;
}
