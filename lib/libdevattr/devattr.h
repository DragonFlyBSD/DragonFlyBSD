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
#include <sys/queue.h>
#include <libprop/proplib.h>
#include <cpu/atomic.h>

struct udev;
struct udev_enumerate;
struct udev_list_entry;
struct udev_monitor;
struct udev_device;

struct udev *udev_new(void);
struct udev *udev_ref(struct udev *udev_ctx);
void udev_unref(struct udev *udev_ctx);
const char *udev_get_dev_path(struct udev *udev_ctx);
void *udev_get_userdata(struct udev *udev_ctx);
void udev_set_userdata(struct udev *udev_ctx, void *userdata);
#ifdef LIBDEVATTR_INTERNAL
int udev_get_fd(struct udev *udev_ctx);
#endif

struct udev_device *udev_device_new_from_dictionary(struct udev *udev_ctx, prop_dictionary_t dict);
struct udev_device *udev_device_ref(struct udev_device *udev_device);
void udev_device_unref(struct udev_device *udev_device);
prop_dictionary_t udev_device_get_dictionary(struct udev_device *udev_device);
struct udev *udev_device_get_udev(struct udev_device *udev_device);
void udev_device_set_action(struct udev_device *udev_device, int action);
const char *udev_device_get_action(struct udev_device *udev_device);
dev_t udev_device_get_devnum(struct udev_device *udev_device);
const char *udev_device_get_devnode(struct udev_device *udev_device);
const char *udev_device_get_property_value(struct udev_device *udev_device,
					const char *key);
const char *udev_device_get_subsystem(struct udev_device *udev_device);
const char *udev_device_get_driver(struct udev_device *udev_device);
uint64_t udev_device_get_kptr(struct udev_device *udev_device);
int32_t udev_device_get_major(struct udev_device *udev_device);
int32_t udev_device_get_minor(struct udev_device *udev_device);

struct udev_enumerate *udev_enumerate_new(struct udev *udev_ctx);
struct udev_enumerate *udev_enumerate_ref(struct udev_enumerate *udev_enum);
void udev_enumerate_unref(struct udev_enumerate *udev_enum);
struct udev *udev_enumerate_get_udev(struct udev_enumerate *udev_enum);
int udev_enumerate_scan_devices(struct udev_enumerate *udev_enum);
struct udev_list_entry *udev_enumerate_get_list_entry(struct udev_enumerate *udev_enum);
#define	udev_list_entry_foreach(list_entry, first_entry) \
	for(list_entry = first_entry; \
	    list_entry != NULL; \
	    list_entry = udev_list_entry_get_next(list_entry))
prop_array_t udev_enumerate_get_array(struct udev_enumerate *udev_enum);
struct udev_list_entry *udev_list_entry_get_next(struct udev_list_entry *list_entry);
prop_dictionary_t udev_list_entry_get_dictionary(struct udev_list_entry *list_entry);
struct udev_device *udev_list_entry_get_device(struct udev_list_entry *list_entry);
#ifdef LIBDEVATTR_INTERNAL
int _udev_enumerate_filter_add_match_gen(struct udev_enumerate *udev_enum,
				     int type,
				     int neg,
				     const char *key,
				     char *expr);
#endif
int udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enum,
						const char *subsystem);
int udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enum,
						const char *subsystem);
int udev_enumerate_add_match_expr(struct udev_enumerate *udev_enum,
			      const char *key,
			      char *expr);
int udev_enumerate_add_nomatch_expr(struct udev_enumerate *udev_enum,
			        const char *key,
			        char *expr);
int udev_enumerate_add_match_regex(struct udev_enumerate *udev_enum,
			      const char *key,
			      char *expr);
int udev_enumerate_add_nomatch_regex(struct udev_enumerate *udev_enum,
			        const char *key,
			        char *expr);
#define udev_enumerate_add_match_property(x, a, b) \
	udev_enumerate_add_match_expr((x), (a), __DECONST(char *, (b)))

#define udev_enumerate_add_nomatch_property(x, a, b) \
	udev_enumerate_add_nomatch_expr((x), (a), __DECONST(char *, (b)))

struct udev_monitor *udev_monitor_new(struct udev *udev_ctx);
struct udev_monitor *udev_monitor_ref(struct udev_monitor *udev_monitor);
void udev_monitor_unref(struct udev_monitor *udev_monitor);
struct udev *udev_monitor_get_udev(struct udev_monitor *udev_monitor);
int udev_monitor_get_fd(struct udev_monitor *udev_monitor);
struct udev_device *udev_monitor_receive_device(struct udev_monitor *udev_monitor);
int udev_monitor_enable_receiving(struct udev_monitor *udev_monitor);
int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
						const char *subsystem,
						const char *devtype);
int udev_monitor_filter_add_match_expr(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr);
int udev_monitor_filter_add_nomatch_expr(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr);
int udev_monitor_filter_add_match_regex(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr);
int udev_monitor_filter_add_nomatch_regex(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr);
#define udev_monitor_filter_add_match_property(x, a, b) \
	udev_monitor_filter_add_match_expr((x), (a), __DECONST(char *, (b)))

#define udev_monitor_filter_add_nomatch_property(x, a, b) \
	udev_monitor_filter_add_nomatch_expr((x), (a), __DECONST(char *, (b)))

#ifdef LIBDEVATTR_INTERNAL
int _udev_monitor_filter_add_match_gen(struct udev_monitor *udev_monitor,
				   int type,
				   int neg,
				   const char *key,
				   char *expr);
int _udev_filter_add_match_gen(prop_array_t filters,
				   int type,
				   int neg,
				   const char *key,
				   char *expr);
#endif

#ifdef LIBDEVATTR_INTERNAL
int send_xml(int s, char *xml);
int read_xml(int s, char **buf);
int _udev_dict_set_cstr(prop_dictionary_t dict, const char *key, char *str);
int _udev_dict_set_int(prop_dictionary_t dict, const char *key, int64_t val);
int _udev_dict_set_uint(prop_dictionary_t dict, const char *key, uint64_t val);
int conn_local_server(const char *sockfile, int socktype, int nonblock,
		  int *retsock);
prop_dictionary_t udevd_get_command_dict(char *command);
prop_array_t udevd_request_devs(int s, prop_array_t filters);
#endif
