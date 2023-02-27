/*
 * Copyright (c)2004,2015 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * fn_subpart.c
 * Installer Function : Create Subpartitions.
 * $Id: fn_subpart.c,v 1.50 2005/04/07 20:22:40 cpressey Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/buffer.h"
#include "libaura/dict.h"
#include "libaura/fspred.h"

#include "libdfui/dfui.h"
#include "libdfui/dump.h"
#include "libdfui/system.h"

#include "libinstaller/commands.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/uiutil.h"

#include "fn.h"
#include "flow.h"
#include "pathnames.h"

#define MTPT_BOOT	0
#define MTPT_SWAP	1
#define MTPT_ROOT	2
#define MTPT_BUILD	3

static int	create_subpartitions(struct i_fn_args *);
static long	default_capacity(struct storage *, const char *);
static int	check_capacity(struct i_fn_args *);
static int	check_subpartition_selections(struct dfui_response *, struct i_fn_args *);
static void	save_subpartition_selections(struct dfui_response *, struct i_fn_args *);
static void	populate_create_subpartitions_form(struct dfui_form *, struct i_fn_args *);
static int	warn_subpartition_selections(struct i_fn_args *);
static struct dfui_form *make_create_subpartitions_form(struct i_fn_args *);
static int	show_create_subpartitions_form(struct dfui_form *, struct i_fn_args *);

static const char *def_mountpt[]  = {"/boot", "swap", "/", "/build", NULL};
static long min_capacity[] = { 128, 0, DISK_MIN - 128, BUILD_MIN };
static int expert = 0;

/*
 * Given a set of subpartitions-to-be in the selected slice,
 * create them.
 */
static int
create_subpartitions(struct i_fn_args *a)
{
	struct subpartition *sp;
	struct commands *cmds;
	int result = 0;
	int num_partitions;

	cmds = commands_new();
	if (!is_file("%sinstall.disklabel.%s",
	    a->tmp,
	    slice_get_device_name(storage_get_selected_slice(a->s)))) {
		/*
		 * Get a copy of the 'virgin' disklabel.
		 * XXX It might make more sense for this to
		 * happen right after format_slice() instead.
		 */
		command_add(cmds, "%s%s -r %s >%sinstall.disklabel.%s",
		    a->os_root, cmd_name(a, "DISKLABEL64"),
		    slice_get_device_name(storage_get_selected_slice(a->s)),
		    a->tmp,
		    slice_get_device_name(storage_get_selected_slice(a->s)));
	}

	/*
	 * Weave together a new disklabel out the of the 'virgin'
	 * disklabel, and the user's subpartition choices.
	 */

	/*
	 * Take everything from the 'virgin' disklabel up until the
	 * '16 partitions' line.
	 */
	num_partitions = 16;
	command_add(cmds, "%s%s '$2==\"partitions:\" || cut { cut = 1 } !cut { print $0 }' <%sinstall.disklabel.%s >%sinstall.disklabel",
	    a->os_root, cmd_name(a, "AWK"),
	    a->tmp,
	    slice_get_device_name(storage_get_selected_slice(a->s)),
	    a->tmp);

	/*
	 * 16 partitions:
	 * #          size     offset    fstype
	 *   c:   16383969          0    unused	#    7999.985MB
	 */

	command_add(cmds, "%s%s '%d partitions:' >>%sinstall.disklabel",
	    a->os_root, cmd_name(a, "ECHO"), num_partitions ,a->tmp);
	command_add(cmds, "%s%s '%s' >>%sinstall.disklabel",
	    a->os_root, cmd_name(a, "ECHO"),
	    "#          size     offset    fstype",
	    a->tmp);

#ifdef DEBUG
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		command_add(cmds, "%s%s 'mountpoint: %s device: %s'",
		     a->os_root, cmd_name(a, "ECHO"),
		     subpartition_get_mountpoint(sp),
		     subpartition_get_device_name(sp));
	}
#endif

	/*
	 * Write a line for each subpartition the user wants.
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_tmpfsbacked(sp)) {
			continue;
		}
		if (subpartition_is_swap(sp)) {
			command_add(cmds, "%s%s '  %c:\t%s\t*\tswap' >>%sinstall.disklabel",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_get_letter(sp),
			    capacity_to_string(subpartition_get_capacity(sp)),
			    a->tmp);
		} else {
			command_add(cmds, "%s%s '  %c:\t%s\t%s\t4.2BSD' >>%sinstall.disklabel",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_get_letter(sp),
			    capacity_to_string(subpartition_get_capacity(sp)),
			    subpartition_get_letter(sp) == 'a' ? "0" : "*",
			    a->tmp);
		}
	}
	temp_file_add(a, "install.disklabel");

	/*
	 * Label the slice from the disklabel we just wove together.
	 */
	command_add(cmds, "%s%s -R -B -r %s %sinstall.disklabel",
	    a->os_root, cmd_name(a, "DISKLABEL64"),
	    slice_get_device_name(storage_get_selected_slice(a->s)),
	    a->tmp);

	/*
	 * Create a snapshot of the disklabel we just created
	 * for debugging inspection in the log.
	 */
	command_add(cmds, "%s%s %s",
	    a->os_root, cmd_name(a, "DISKLABEL64"),
	    slice_get_device_name(storage_get_selected_slice(a->s)));

	/*
	 * If encryption was specified, read the passphrase.
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_encrypted(sp)) {
			fn_get_passphrase(a, 1);
			break;
		}
	}

	/*
	 * Create filesystems on the newly-created subpartitions.
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_swap(sp) || subpartition_is_tmpfsbacked(sp)) {
			if (subpartition_is_swap(sp) &&
			    subpartition_is_encrypted(sp)) {
				command_add(cmds,
				    "%s%s -d /tmp/t1 luksFormat /dev/%s",
				    a->os_root, cmd_name(a, "CRYPTSETUP"),
				    subpartition_get_device_name(sp));
				command_add(cmds,
				    "%s%s -d /tmp/t1 luksOpen /dev/%s swap",
				    a->os_root, cmd_name(a, "CRYPTSETUP"),
				    subpartition_get_device_name(sp));
			}
			continue;
		}

		if (subpartition_is_encrypted(sp)) {
			command_add(cmds,
			    "%s%s -d /tmp/t1 luksFormat /dev/%s",
			    a->os_root, cmd_name(a, "CRYPTSETUP"),
			    subpartition_get_device_name(sp));
			command_add(cmds,
			    "%s%s -d /tmp/t1 luksOpen /dev/%s %s",
			    a->os_root, cmd_name(a, "CRYPTSETUP"),
			    subpartition_get_device_name(sp),
			    subpartition_get_mapper_name(sp, -1));
			command_add(cmds, "%s%s%s -b %ld -f %ld /dev/mapper/%s",
			    a->os_root, cmd_name(a, "NEWFS"),
			    subpartition_is_softupdated(sp) ? " -U" : "",
			    subpartition_get_bsize(sp),
			    subpartition_get_fsize(sp),
			    subpartition_get_mapper_name(sp, -1));
		} else {
			command_add(cmds, "%s%s%s -b %ld -f %ld /dev/%s",
			    a->os_root, cmd_name(a, "NEWFS"),
			    subpartition_is_softupdated(sp) ? " -U" : "",
			    subpartition_get_bsize(sp),
			    subpartition_get_fsize(sp),
			    subpartition_get_device_name(sp));
		}
	}

	result = commands_execute(a, cmds);
	commands_free(cmds);
	unlink("/tmp/t1");

	return(result);
}

/*
 * Return default capacity field filler.  Return 0 for /build if drive
 * space minus swap is < 40GB (causes installer to use PFS's on the root
 * partition instead).
 */
static long
default_capacity(struct storage *s, const char *mtpt)
{
	unsigned long boot, root, swap, build;
	unsigned long capacity;
	unsigned long mem;

	capacity = slice_get_capacity(storage_get_selected_slice(s)); /* MB */
	mem = storage_get_memsize(s);

	/*
	 * Slice capacity is at least 10G at this point.  Calculate basic
	 * defaults.
	 */
	swap = 2 * mem;
	if (swap > capacity / 10)	/* max 1/10 capacity */
		swap = capacity / 10;
	if (swap < SWAP_MIN)		/* having a little is nice */
		swap = SWAP_MIN;
	if (swap > SWAP_MAX)		/* installer cap */
		swap = SWAP_MAX;

	boot = 1024;

	build = (capacity - swap - boot) / 4;
#if 0
	/*
	 * No longer cap the size of /build, the assumption didn't hold
	 * well particularly with /var/crash being placed on /build now.
	 */
	if (build > BUILD_MAX)
		build = BUILD_MAX;
#endif

	for (;;) {
		root = (capacity - swap - boot - build);

		/*
		 * Adjust until the defaults look sane
		 *
		 * root should be at least twice as large as build
		 */
		if (build && root < build * 2) {
			--build;
			continue;
		}

		/*
		 * root should be at least 1/2 capacity
		 */
		if (build && root < capacity / 2) {
			--build;
			continue;
		}
		break;
	}

	/*
	 * Finalize.  If build is too small do not supply a /build,
	 * and if swap is too small do not supply swap.  Cascade the
	 * released space to swap and root.
	 */
	if (build < BUILD_MIN) {
		if (swap < SWAP_MIN && build >= SWAP_MIN - swap) {
			build -= SWAP_MIN - swap;
			swap = SWAP_MIN;
		}
		if (swap < 2 * mem && build >= 2 * mem - swap) {
			build -= 2 * mem - swap;
			swap = 2 * mem;
		}
		root += build;
		build = 0;
	}
	if (swap < SWAP_MIN) {
		root += swap;
		swap = 0;
	}

	if (build == 0)
		root = -1;	/* no /build, root is the last part */
	else
		build = -1;	/* last partition just use remaining space */

	if (strcmp(mtpt, "/boot") == 0)
		return(boot);
	else if (strcmp(mtpt, "/build") == 0)
		return(build);
	else if (strcmp(mtpt, "swap") == 0)
		return(swap);
	else if (strcmp(mtpt, "/") == 0)
		return(root);

	/* shouldn't ever happen */
	return(-1);
}

static int
check_capacity(struct i_fn_args *a)
{
	struct subpartition *sp;
	unsigned long total_capacity = 0;
	int mtpt;

	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		long subpart_capacity = subpartition_get_capacity(sp);
		const char *mountpt = subpartition_get_mountpoint(sp);

		if (subpart_capacity == -1)
			total_capacity++;
		else
			total_capacity += subpart_capacity;
		for (mtpt = 0; def_mountpt[mtpt] != NULL; mtpt++) {
			if (strcmp(mountpt, def_mountpt[mtpt]) == 0 &&
			    subpart_capacity < min_capacity[mtpt] &&
			    subpart_capacity != -1) {
				inform(a->c, _("WARNING: The size (%ldM) specified for "
				    "the %s subpartition is too small. It "
				    "should be at least %ldM or you will "
				    "risk running out of space during "
				    "the installation."),
				    subpart_capacity, mountpt,
				    min_capacity[mtpt]);
			}
		}
	}

	if (total_capacity > slice_get_capacity(storage_get_selected_slice(a->s))) {
		inform(a->c, _("The space allocated to all of your selected "
		    "subpartitions (%luM) exceeds the total "
		    "capacity of the selected primary partition "
		    "(%luM). Remove some subpartitions or choose "
		    "a smaller size for them and try again."),
		    total_capacity, slice_get_capacity(storage_get_selected_slice(a->s)));
		return(0);
	}

	return(1);
}

static int
check_subpartition_selections(struct dfui_response *r, struct i_fn_args *a)
{
	struct dfui_dataset *ds;
	struct dfui_dataset *star_ds = NULL;
	struct aura_dict *d;
	const char *mountpoint, *capstring;
	long capacity = 0;
	int found_root = 0;
	int valid = 1;

	d = aura_dict_new(1, AURA_DICT_LIST);

	if ((ds = dfui_response_dataset_get_first(r)) == NULL) {
		inform(a->c, _("Please set up at least one subpartition."));
		valid = 0;
	}

	for (ds = dfui_response_dataset_get_first(r); valid && ds != NULL;
	    ds = dfui_dataset_get_next(ds)) {
#ifdef DEBUG
		dfui_dataset_dump(ds);
#endif
		mountpoint = dfui_dataset_get_value(ds, "mountpoint");
		capstring = dfui_dataset_get_value(ds, "capacity");

		if (expert) {
			int tmpfsbacked;

			tmpfsbacked = (strcmp(dfui_dataset_get_value(ds, "tmpfsbacked"), "Y") == 0);
			if (tmpfsbacked && strcmp(mountpoint, "/") == 0) {
				inform(a->c, _("/ cannot be TMPFS backed."));
				valid = 0;
			}
		}

		if (aura_dict_exists(d, mountpoint, strlen(mountpoint) + 1)) {
			inform(a->c, _("The same mount point cannot be specified "
			    "for two different subpartitions."));
			valid = 0;
		}

		if (strcmp(mountpoint, "/") == 0)
			found_root = 1;

		if (strcmp(capstring, "*") == 0) {
			if (star_ds != NULL) {
				inform(a->c, _("You cannot have more than one subpartition "
				    "with a '*' capacity (meaning 'use the remainder "
				    "of the primary partition'.)"));
				valid = 0;
			} else {
				star_ds = ds;
			}
		}

		if (!(!strcasecmp(mountpoint, "swap") || mountpoint[0] == '/')) {
			inform(a->c, _("Mount point must be either 'swap', or it must "
			    "start with a '/'."));
			valid = 0;
		}

		if (strpbrk(mountpoint, " \\\"'`") != NULL) {
			inform(a->c, _("Mount point may not contain the following "
			    "characters: blank space, backslash, or "
			    "single, double, or back quotes."));
			valid = 0;
		}

		if (strlen(capstring) == 0) {
			inform(a->c, _("A capacity must be specified."));
			valid = 0;
		}

		if (!string_to_capacity(capstring, &capacity)) {
			inform(a->c, _("Capacity must be either a '*' symbol "
			    "to indicate 'use the rest of the primary "
			    "partition', or it must be a series of decimal "
			    "digits ending with an 'M' (indicating "
			    "megabytes), a 'G' (indicating gigabytes) and "
			    "so on (up to 'E'.)"));
			valid = 0;
		}

		/*
		 * Maybe remove this limit entirely?
		 */
		if ((strcasecmp(mountpoint, "swap") == 0) &&
		    (capacity > SWAP_MAX)) {
			inform(a->c, _("Swap capacity is limited to %dG."),
			    SWAP_MAX / 1024);
			valid = 0;
		}

		/*
		 * If we made it through that obstacle course, all is well.
		 */

		if (valid)
			aura_dict_store(d, mountpoint, strlen(mountpoint) + 1, "", 1);
	}

	if (!found_root) {
		inform(a->c, _("You must include a / (root) subpartition."));
		valid = 0;
	}

	if (aura_dict_size(d) > 16) {
		inform(a->c, _("You cannot have more than 16 subpartitions "
		    "on a single primary partition.  Remove some "
		    "and try again."));
		valid = 0;
	}

	aura_dict_free(d);

	return(valid);
}

static void
save_subpartition_selections(struct dfui_response *r, struct i_fn_args *a)
{
	struct dfui_dataset *ds;
	char tmpfsbacked;
	const char *mountpoint, *capstring;
	long capacity;
	long bsize, fsize;
	int softupdates;
	int valid = 1;

	subpartitions_free(storage_get_selected_slice(a->s));

	for (ds = dfui_response_dataset_get_first(r); valid && ds != NULL;
	    ds = dfui_dataset_get_next(ds)) {
		mountpoint = dfui_dataset_get_value(ds, "mountpoint");
		capstring = dfui_dataset_get_value(ds, "capacity");

		if (expert) {
			softupdates =
			    (strcmp(dfui_dataset_get_value(ds, "softupdates"), "Y") == 0);
			fsize = atol(dfui_dataset_get_value(ds, "fsize"));
			bsize = atol(dfui_dataset_get_value(ds, "bsize"));
			tmpfsbacked = (strcmp(dfui_dataset_get_value(ds, "tmpfsbacked"), "Y") == 0);
		} else {
			softupdates = (strcmp(mountpoint, "/boot") == 0 ? 0 : 1);
			tmpfsbacked = 0;
			fsize = -1;
			bsize = -1;
		}

		if (string_to_capacity(capstring, &capacity)) {
			subpartition_new_ufs(storage_get_selected_slice(a->s),
			    mountpoint, capacity,
			    strcasecmp(dfui_dataset_get_value(ds, "encrypted"), "Y") == 0,
			    softupdates, fsize, bsize, tmpfsbacked);
		}
	}
}

static void
populate_create_subpartitions_form(struct dfui_form *f, struct i_fn_args *a)
{
	struct subpartition *sp;
	struct dfui_dataset *ds;
	char temp[32];
	int mtpt;
	long capacity;

	if (slice_subpartition_first(storage_get_selected_slice(a->s)) != NULL) {
		/*
		 * The user has already given us their subpartition
		 * preferences, so use them here.
		 */
		for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
		     sp != NULL; sp = subpartition_next(sp)) {
			ds = dfui_dataset_new();
			dfui_dataset_celldata_add(ds, "mountpoint",
			    subpartition_get_mountpoint(sp));
			dfui_dataset_celldata_add(ds, "capacity",
			    capacity_to_string(subpartition_get_capacity(sp)));
			dfui_dataset_celldata_add(ds, "encrypted",
			    subpartition_is_encrypted(sp) ? "Y" : "N");
			if (expert) {
				dfui_dataset_celldata_add(ds, "softupdates",
				    subpartition_is_softupdated(sp) ? "Y" : "N");
				dfui_dataset_celldata_add(ds, "tmpfsbacked",
				    subpartition_is_tmpfsbacked(sp) ? "Y" : "N");
				snprintf(temp, 32, "%ld", subpartition_get_fsize(sp));
				dfui_dataset_celldata_add(ds, "fsize",
				    temp);
				snprintf(temp, 32, "%ld", subpartition_get_bsize(sp));
				dfui_dataset_celldata_add(ds, "bsize",
				    temp);
			}
			dfui_form_dataset_add(f, ds);
		}
	} else {
		/*
		 * Otherwise, populate the form with datasets representing
		 * reasonably-calculated defaults.  The defaults are chosen
		 * based on the slice's total capacity and the machine's
		 * total physical memory (for swap.)
		 */
		for (mtpt = 0; def_mountpt[mtpt] != NULL; mtpt++) {
			capacity = default_capacity(a->s, def_mountpt[mtpt]);
			if (capacity == 0)
				continue;
			ds = dfui_dataset_new();
			dfui_dataset_celldata_add(ds, "mountpoint",
			    def_mountpt[mtpt]);
			dfui_dataset_celldata_add(ds, "capacity",
			    capacity_to_string(capacity));
			dfui_dataset_celldata_add(ds, "encrypted", "N");
			if (expert) {
				dfui_dataset_celldata_add(ds, "softupdates",
				    strcmp(def_mountpt[mtpt], "/boot") != 0 ? "Y" : "N");
				dfui_dataset_celldata_add(ds, "tmpfsbacked",
				    "N");
				dfui_dataset_celldata_add(ds, "fsize",
				    capacity < 1024 ? "1024" : "2048");
				dfui_dataset_celldata_add(ds, "bsize",
				    capacity < 1024 ? "8192" : "16384");
			}
			dfui_form_dataset_add(f, ds);
		}
	}
}

static int
warn_subpartition_selections(struct i_fn_args *a)
{
	int valid = 0;

	/* Skip this check for disks <= 8GB */
	if (slice_get_capacity(storage_get_selected_slice(a->s)) <= 8192)
		return 0;

	valid = check_capacity(a);

	return(!valid);
}

static struct dfui_form *
make_create_subpartitions_form(struct i_fn_args *a)
{
	struct dfui_field *fi;
	struct dfui_form *f;
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Subpartitions further divide a primary partition for "
	    "use with %s.  Some reasons you may want "
	    "a set of subpartitions are:\n\n"
	    "- you want to restrict how much data can be written "
	    "to certain parts of the primary partition, to quell "
	    "denial-of-service attacks; and\n"
	    "- you want to speed up access to data on the disk."
	    ""), OPERATING_SYSTEM_NAME);

	f = dfui_form_create(
	    "create_subpartitions",
	    _("Create Subpartitions"),
	    _("Set up the subpartitions (also known as just `partitions' "
	    "in BSD tradition) you want to have on this primary "
	    "partition.\n\n"
	    "For Capacity, use 'M' to indicate megabytes, 'G' to "
	    "indicate gigabytes, and so on (up to 'E'.) A single '*' "
	    "indicates 'use the remaining space on the primary partition'."),

	    msg_buf[0],

	    "p", "special", "dfinstaller_create_subpartitions",
	    "p", "minimum_width","64",

	    "f", "mountpoint", _("Mountpoint"), "", "",
	    "f", "capacity", _("Capacity"), "", "",

	    "f", "encrypted", _("Encrypted"), "", "",
	    "p", "control", "checkbox",

	    "a", "ok", _("Accept and Create"), "", "",
	    "a", "cancel",
	    (disk_get_formatted(storage_get_selected_disk(a->s)) ?
	    _("Return to Select Disk") :
	    _("Return to Select Primary Partition")), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	dfui_form_set_multiple(f, 1);
	dfui_form_set_extensible(f, 1);

	if (expert) {
		fi = dfui_form_field_add(f, "softupdates",
		    dfui_info_new(_("Softupdates"), "", ""));
		dfui_field_property_set(fi, "control", "checkbox");

		fi = dfui_form_field_add(f, "tmpfsbacked",
		    dfui_info_new(_("TMPFS"), "", ""));
		dfui_field_property_set(fi, "control", "checkbox");

		fi = dfui_form_field_add(f, "fsize",
		    dfui_info_new(_("Frag Sz"), "", ""));

		fi = dfui_form_field_add(f, "bsize",
		    dfui_info_new(_("Block Sz"), "", ""));

		dfui_form_action_add(f, "switch",
		    dfui_info_new(_("Switch to Normal Mode"), "", ""));
	} else {
		dfui_form_action_add(f, "switch",
		    dfui_info_new(_("Switch to Expert Mode"), "", ""));
	}

	return(f);
}

/*
 * Returns:
 *	-1 = the form should be redisplayed
 *	 0 = failure, function is over
 *	 1 = success, function is over
 */
static int
show_create_subpartitions_form(struct dfui_form *f, struct i_fn_args *a)
{
	struct dfui_dataset *ds;
	struct dfui_response *r;

	for (;;) {
		if (dfui_form_dataset_get_first(f) == NULL)
			populate_create_subpartitions_form(f, a);

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			dfui_response_free(r);
			return(0);
		} else if (strcmp(dfui_response_get_action_id(r), "switch") == 0) {
			if (check_subpartition_selections(r, a)) {
				save_subpartition_selections(r, a);
				expert = expert ? 0 : 1;
				dfui_response_free(r);
				return(-1);
			}
		} else {
			if (check_subpartition_selections(r, a)) {
				save_subpartition_selections(r, a);
				if (!warn_subpartition_selections(a)) {
					if (!create_subpartitions(a)) {
						inform(a->c, _("The subpartitions you chose were "
							"not correctly created, and the "
							"primary partition may "
							"now be in an inconsistent state. "
							"We recommend re-formatting it "
							"before proceeding."));
						dfui_response_free(r);
						return(0);
					} else {
						dfui_response_free(r);
						return(1);
					}
				}
			}
		}

		dfui_form_datasets_free(f);
		/* dfui_form_datasets_add_from_response(f, r); */
		for (ds = dfui_response_dataset_get_first(r); ds != NULL;
		    ds = dfui_dataset_get_next(ds)) {
			dfui_form_dataset_add(f, dfui_dataset_dup(ds));
		}
	}
}

void
fn_create_subpartitions_ufs(struct i_fn_args *a)
{
	struct dfui_form *f;
	unsigned long capacity;
	int done = 0;

	a->result = 0;
	capacity = slice_get_capacity(storage_get_selected_slice(a->s));
	if (capacity < DISK_MIN) {
		inform(a->c, _("The selected disk is smaller than the "
		    "required %dM for the UFS filesystem."), DISK_MIN);
		return;
	}
#if 0
	if (capacity <= 8192)
		def_mountpt[2] = NULL; /* XXX adjust each time in a session */
#endif
	while (!done) {
		f = make_create_subpartitions_form(a);
		switch (show_create_subpartitions_form(f, a)) {
		case -1:
			done = 0;
			break;
		case 0:
			done = 1;
			a->result = 0;
			break;
		case 1:
			done = 1;
			a->result = 1;
			break;
		}
		dfui_form_free(f);
	}
}
