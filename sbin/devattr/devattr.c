/*
 * Copyright (c) 2010
 *		The DragonFly Project.	All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Nolan Lum <nol888@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in
 *	  the documentation and/or other materials provided with the
 *	  distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *	  contributors may be used to endorse or promote products derived
 *	  from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.	 IN NO EVENT SHALL THE
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

#include <ctype.h>
#include <devattr.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

static SLIST_HEAD(, sl_entry) props = SLIST_HEAD_INITIALIZER(props);
struct sl_entry {
	char *val;
	SLIST_ENTRY(sl_entry) entries;
} *ent;

static void
usage(const char *name)
{
	fprintf(stderr,
	    "usage: %s [-Ah] [-p property] [-d device] [-m key:value] [-r key:value]\n"
	    "Valid options are:\n"
	    " -A\n"
	    "	Don't display aliases.\n"
	    " -h\n"
	    "	Print this help message.\n\n"
	    "Valid options with their arguments are:\n"
	    " -p <property>\n"
	    "	Only display property; can be specified multiple times and\n"
	    "	combined with all other options.\n"
	    " -d <device>\n"
	    "	Only display devices with name `device'. When used with\n"
	    "	-p, only properties `-p' of device `-d' are listed. Can be\n"
	    "	specified multiple times. Allows wildcards.\n"
	    " -m <key:value>\n"
	    "	Only display devices whose property `key' matches with wildcards\n"
	    "	value `value' unless the key-value pair starts with ~, in which\n"
	    "	case the match is inverted. Stacks with -p, -d, and -m.\n"
	    "	Can be specified multiple times.\n"
	    " -r <key:value>\n"
	    "	Behaves similarly to `-m', but matches with regex.\n",
	    name);
	exit(EX_USAGE);
}

static void
parse_args(int argc, char *argv[], struct udev_enumerate *enumerate)
{
	int ch, invert;
	char *colon;

	SLIST_INIT(&props);

	/* A = no aliases */
	/* p = properties to list (defaults to all) */
	/* d = devices to list (defaults to all) */
	/* m = display only devices in d that match these prop values */
	/* r = display only devices in d that match these prop values (regex) */
	while ((ch = getopt(argc, argv, "Ap:d:m:r:h")) != -1) {
		invert = false;

		switch (ch) {
		case 'A':
			udev_enumerate_add_match_property(enumerate, "alias",
			    "0");
			break;
		case 'p':
			ent = malloc(sizeof(struct sl_entry));
			ent->val = optarg;
			SLIST_INSERT_HEAD(&props, ent, entries);
			break;
		case 'd':
			udev_enumerate_add_match_expr(enumerate, "name",
			    optarg);
			break;
		case 'm':
		case 'r':
			/* Check for exclusion. */
			invert = *optarg == '~';

			/* Split into key/value. */
			colon = strchr(optarg, ':');
			if (colon == NULL) {
				fprintf(stderr,
				    "Invalid property key/value pair `%s'.\n",
				    optarg);
				return;
			}

			*colon = '\0';
			if (invert) {
				if (ch == 'r')
					udev_enumerate_add_nomatch_regex(enumerate,
					    optarg + 1, colon + 1);
				else
					udev_enumerate_add_nomatch_expr(enumerate,
					    optarg + 1, colon + 1);
			} else {
				if (ch == 'r')
					udev_enumerate_add_match_regex(enumerate,
					    optarg, colon + 1);
				else
					udev_enumerate_add_match_expr(enumerate,
					    optarg, colon + 1);
			}
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}
	return;
}

static void
print_prop(const char* key, prop_object_t value)
{
	char *val_str;

	printf("\t%s = ", key);

	prop_type_t val_type = prop_object_type(value);
	switch (val_type) {
	case PROP_TYPE_BOOL:
		printf("%s\n", prop_bool_true((prop_bool_t)value) ?
		    "true" : "false");
		break;
	case PROP_TYPE_NUMBER:
		if (prop_number_unsigned((prop_number_t)value))
			printf("%1$"PRIu64" (0x%1$"PRIx64")\n",
			    prop_number_unsigned_integer_value((prop_number_t)value));
		else
			printf("%"PRId64"\n",
			    prop_number_integer_value((prop_number_t)value));
		break;
	case PROP_TYPE_STRING:
		val_str = prop_string_cstring(value);
		printf("%s\n", val_str);
		free(val_str);
		break;
	default:
		break;
	}
}

int
main(int argc, char* argv[])
{
	struct udev *ctx;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *current;
	struct udev_device *dev;
	prop_object_t key_val;
	prop_dictionary_t dict;
	prop_dictionary_keysym_t cur_key;
	prop_object_iterator_t iter;
	const char *key_str;
	char *dev_name;
	int ret;

	ctx = udev_new();
	if (ctx == NULL)
		err(EX_UNAVAILABLE, "udev_new");

	enumerate = udev_enumerate_new(ctx);
	if (enumerate == NULL)
		err(EX_UNAVAILABLE, "udev_enumerate_new");

	parse_args(argc, argv, enumerate);

	ret = udev_enumerate_scan_devices(enumerate);
	if (ret != 0)
		err(EX_UNAVAILABLE, "udev_enumerate_scan_devices ret = %d",
		    ret);

	current = udev_enumerate_get_list_entry(enumerate);
	if (current == NULL) {
		printf("No devices found.\n");
	} else {
		udev_list_entry_foreach(current, current) {
			dev = udev_list_entry_get_device(current);
			if (dev == NULL)
				continue;
			dict = udev_device_get_dictionary(dev);
			if (dict == NULL)
				continue;
			iter = prop_dictionary_iterator(dict);
			cur_key = NULL;

			dev_name = prop_string_cstring(prop_dictionary_get(dict, "name"));
			printf("Device %s:\n", dev_name);
			free(dev_name);

			if (!SLIST_EMPTY(&props)) {
				SLIST_FOREACH(ent, &props, entries) {
					key_val = prop_dictionary_get(dict,
					    ent->val);
					if (key_val != NULL)
						print_prop(ent->val, key_val);
				}
			} else {
				while ((cur_key = (prop_dictionary_keysym_t)prop_object_iterator_next(iter)) != NULL) {
					key_str = prop_dictionary_keysym_cstring_nocopy(cur_key);
					key_val = prop_dictionary_get_keysym(dict,
					    cur_key);
					print_prop(key_str, key_val);
				}
			}

			printf("\n");
		}
	}

	udev_enumerate_unref(enumerate);
	udev_unref(ctx);

	return (0);
}
