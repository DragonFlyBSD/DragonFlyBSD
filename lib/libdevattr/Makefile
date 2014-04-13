LIB=	devattr
SHLIBDIR?= /lib
SRCS=	devattr.c devattr_device.c devattr_enumerate.c devattr_monitor.c
INCS=	devattr.h

LDADD+=	-lprop
DPADD+=	${LIBPROP}

MAN=	devattr.3 udev.3 udev_device.3 udev_enumerate.3 udev_monitor.3
MLINKS+=	udev.3	udev_new.3
MLINKS+=	udev.3	udev_ref.3
MLINKS+=	udev.3	udev_unref.3
MLINKS+=	udev.3	udev_get_dev_path.3
MLINKS+=	udev.3	udev_set_userdata.3
MLINKS+=	udev.3	udev_get_userdata.3

MLINKS+=	udev_device.3	udev_device_ref.3
MLINKS+=	udev_device.3	udev_device_unref.3
MLINKS+=	udev_device.3	udev_device_get_dictionary.3
MLINKS+=	udev_device.3	udev_device_get_udev.3
MLINKS+=	udev_device.3	udev_device_get_action.3
MLINKS+=	udev_device.3	udev_device_get_devnum.3
MLINKS+=	udev_device.3	udev_device_get_devnode.3
MLINKS+=	udev_device.3	udev_device_get_driver.3
MLINKS+=	udev_device.3	udev_device_get_subsystem.3
MLINKS+=	udev_device.3	udev_device_get_kptr.3
MLINKS+=	udev_device.3	udev_device_get_major.3
MLINKS+=	udev_device.3	udev_device_get_minor.3
MLINKS+=	udev_device.3	udev_device_get_property_value.3

MLINKS+=	udev_enumerate.3	udev_enumerate_new.3
MLINKS+=	udev_enumerate.3	udev_enumerate_ref.3
MLINKS+=	udev_enumerate.3	udev_enumerate_unref.3
MLINKS+=	udev_enumerate.3	udev_enumerate_get_udev.3
MLINKS+=	udev_enumerate.3	udev_enumerate_scan_devices.3
MLINKS+=	udev_enumerate.3	udev_enumerate_get_array.3
MLINKS+=	udev_enumerate.3	udev_enumerate_get_list_entry.3
MLINKS+=	udev_enumerate.3	udev_list_entry_get_next.3
MLINKS+=	udev_enumerate.3	udev_list_entry_foreach.3
MLINKS+=	udev_enumerate.3	udev_list_entry_get_dictionary.3
MLINKS+=	udev_enumerate.3	udev_list_entry_get_device.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_match_subsystem.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_nomatch_subsystem.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_match_expr.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_match_property.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_nomatch_expr.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_nomatch_property.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_match_regex.3
MLINKS+=	udev_enumerate.3	udev_enumerate_add_nomatch_regex.3

MLINKS+=	udev_monitor.3	udev_monitor_new.3
MLINKS+=	udev_monitor.3	udev_monitor_ref.3
MLINKS+=	udev_monitor.3	udev_monitor_unref.3
MLINKS+=	udev_monitor.3	udev_monitor_get_udev.3
MLINKS+=	udev_monitor.3	udev_monitor_get_fd.3
MLINKS+=	udev_monitor.3	udev_monitor_receive_device.3
MLINKS+=	udev_monitor.3	udev_monitor_enable_receiving.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_match_subsystem_devtype.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_match_expr.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_nomatch_expr.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_match_property.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_nomatch_property.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_match_regex.3
MLINKS+=	udev_monitor.3	udev_monitor_filter_add_nomatch_regex.3


.include <bsd.lib.mk>
