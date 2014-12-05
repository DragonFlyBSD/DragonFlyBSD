/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
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
 * fn_disk.c
 * Disk functions for installer.
 * $Id: fn_disk.c,v 1.40 2005/03/13 01:53:58 cpressey Exp $
 */

#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/fspred.h"

#include "libdfui/dfui.h"
#include "libdfui/system.h"

#include "libinstaller/commands.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/uiutil.h"

#include "fn.h"
#include "pathnames.h"

/*** DISK-RELATED FUNCTIONS ***/

/*
 * Ask the user which physical disk they want.
 * Changes ss->selected_disk if successful.
 */
void
fn_select_disk(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	struct disk *d;

	f = dfui_form_create(
	    "select_disk",
	    _("Select Disk"),
	    a->short_desc,
	    "",

	    "p", "role",  "menu",
	    "p", "special", "dfinstaller_select_disk",

	    NULL
	);

	for (d = storage_disk_first(a->s); d != NULL; d = disk_next(d)) {
		dfui_form_action_add(f, disk_get_device_name(d),
		    dfui_info_new(disk_get_desc(d), "", ""));
	}

	k = dfui_form_action_add(f, "cancel",
	    dfui_info_new(a->cancel_desc, "", ""));
	dfui_action_property_set(k, "accelerator", "ESC");

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		a->result = 0;
	} else {
		d = disk_find(a->s, dfui_response_get_action_id(r));
		if (d == NULL) {
			inform(a->c, _("Internal error - response from frontend "
			    "should be a valid device name."));
			a->result = 0;
		} else {
			storage_set_selected_disk(a->s, d);
			a->result = 1;
		}
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 * Ask the user which slice on a the selected disk they want.
 * Changes ss->selected_slice.
 */
void
fn_select_slice(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	struct slice *s;
	char string[16];

	f = dfui_form_create(
	    "select_slice",
	    _("Select Primary Partition"),
	    a->short_desc,
	    "",

	    "p", "role", "menu",
	    "p", "special", "dfinstaller_select_slice",

	    NULL
	);

	for (s = disk_slice_first(storage_get_selected_disk(a->s));
	     s != NULL; s = slice_next(s)) {
		snprintf(string, 16, "%d", slice_get_number(s));
		dfui_form_action_add(f, string,
		    dfui_info_new(slice_get_desc(s), "", ""));
	}

	k = dfui_form_action_add(f, "cancel",
	    dfui_info_new(a->cancel_desc, "", ""));
	dfui_action_property_set(k, "accelerator", "ESC");

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		a->result = 0;
	} else {
		s = slice_find(storage_get_selected_disk(a->s),
		    atoi(dfui_response_get_action_id(r)));
		if (s == NULL) {
			inform(a->c, _("Internal error - response from frontend "
			    "should be a valid slice number."));
			a->result = 0;
		} else {
			storage_set_selected_slice(a->s, s);
			a->result = 1;
		}
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 * If ss->selected_disk == NULL, user will be asked for which disk.
 * Returns 1 if disk was formatted, 0 if it wasn't.
 * If it was, ss->selected_disk and ss->selected_slice are set to it.
 */
void
fn_format_disk(struct i_fn_args *a)
{
	struct commands *cmds;
	char *selected_disk_string;

	if (storage_get_selected_disk(a->s) == NULL) {
		a->short_desc = _("Select a disk to format.");
		a->cancel_desc = _("Return to Utilities Menu");
		fn_select_disk(a);
		if (!a->result || storage_get_selected_disk(a->s) == NULL) {
			a->result = 0;
			return;
		}
	}

	if (confirm_dangerous_action(a->c,
	    _("WARNING!  ALL data in ALL partitions on the disk\n\n"
	    "%s\n\nwill be IRREVOCABLY ERASED!\n\nAre you ABSOLUTELY "
	    "SURE you wish to take this action?  This is your "
	    "LAST CHANCE to cancel!"), disk_get_desc(storage_get_selected_disk(a->s)))) {
		cmds = commands_new();

		command_add(cmds, "%s%s -BI %s",
		    a->os_root, cmd_name(a, "FDISK"),
		    disk_get_device_name(storage_get_selected_disk(a->s)));

		if (!commands_execute(a, cmds)) {
			inform(a->c, _("The disk\n\n%s\n\nwas "
			    "not correctly formatted, and may "
			    "now be in an inconsistent state. "
			    "We recommend re-formatting it "
			    "before attempting to install "
			    "%s on it."),
			    disk_get_desc(storage_get_selected_disk(a->s)),
			    OPERATING_SYSTEM_NAME);
			commands_free(cmds);
			a->result = 0;
			return;
		}
		commands_free(cmds);

		/*
		 * Since one of the disks has now changed, we must
		 * refresh our view of them and re-select the disk
		 * since the selected_disk pointer will be invalidated.
		 */
		selected_disk_string = aura_strdup(
		    disk_get_device_name(storage_get_selected_disk(a->s)));
		if (!survey_storage(a)) {
			inform(a->c, _("Errors occurred while probing "
			    "the system for its storage capabilities."));
		}
		storage_set_selected_disk(a->s, disk_find(a->s, selected_disk_string));
		free(selected_disk_string);

		/*
		 * Note that we formatted this disk and that we want
		 * to use the first (and only) slice of it.
		 */
		disk_set_formatted(storage_get_selected_disk(a->s), 1);
		storage_set_selected_slice(a->s, disk_slice_first(storage_get_selected_disk(a->s)));

		if (!format_slice(a)) {
			inform(a->c, _("The sole primary partition of "
			    "the disk\n\n%s\n\nwas "
			    "not correctly formatted, and may "
			    "now be in an inconsistent state. "
			    "We recommend re-formatting the "
			    "disk before attempting to install "
			    "%s on it."),
			    disk_get_desc(storage_get_selected_disk(a->s)),
			    OPERATING_SYSTEM_NAME);
			a->result = 0;
			return;
		}

		inform(a->c, _("The disk\n\n%s\n\nwas formatted."),
		    disk_get_desc(storage_get_selected_disk(a->s)));
		a->result = 1;
	} else {
		inform(a->c, _("Action cancelled - no disks were formatted."));
		a->result = 0;
	}
}

/*
 * Wipes the start of the selected disk.
 */
void
fn_wipe_start_of_disk(struct i_fn_args *a)
{
	struct commands *cmds;

	a->short_desc = _("If you are having problems formatting a disk, "
	    "it may be because of junk that has accumulated "
	    "in the boot block and the partition table. "
	    "A cure for this is to wipe out everything on "
	    "the first few sectors of the disk.  However, this "
	    "is a rather drastic action to take, so it is not "
	    "recommended unless you are otherwise "
	    "encountering problems.");
	a->cancel_desc = _("Return to Utilities Menu");
	fn_select_disk(a);
	if (!a->result)
		return;

	/* XXX check to make sure no slices on this disk are mounted first? */
	if (storage_get_selected_disk(a->s) != NULL && confirm_dangerous_action(a->c,
	    _("WARNING!  ALL data in ALL partitions on the disk\n\n"
	    "%s\n\nwill be IRREVOCABLY ERASED!\n\nAre you ABSOLUTELY "
	    "SURE you wish to take this action?  This is your "
	    "LAST CHANCE to cancel!"), disk_get_desc(storage_get_selected_disk(a->s)))) {
		cmds = commands_new();
		command_add(cmds,
		    "%s%s if=/dev/zero of=/dev/%s bs=32k count=16",
		    a->os_root, cmd_name(a, "DD"),
		    disk_get_device_name(storage_get_selected_disk(a->s)));
		if (commands_execute(a, cmds)) {
			inform(a->c, _("Start of disk was successfully wiped."));
		} else {
			inform(a->c, _("Some errors occurred. "
			    "Start of disk was not successfully wiped."));
		}
		commands_free(cmds);
	}
}

/*
 * Wipes the start of the selected slice.
 */
void
fn_wipe_start_of_slice(struct i_fn_args *a)
{
	struct commands *cmds;

	a->short_desc =
	  _("If you are having problems formatting a primary partition, "
	    "it may be because of junk that has accumulated in the "
	    "partition's `disklabel'. A cure for this is to wipe out "
	    "everything on the first few sectors of the primary partition. "
	    "However, this is a rather drastic action to take, so it is not "
	    "recommended unless you are otherwise encountering problems.");
	a->cancel_desc = _("Return to Utilities Menu");
	fn_select_slice(a);
	if (!a->result)
		return;

	if (confirm_dangerous_action(a->c,
	    _("WARNING!  ALL data in primary partition #%d,\n\n%s\n\non the "
	    "disk\n\n%s\n\n will be IRREVOCABLY ERASED!\n\nAre you "
	    "ABSOLUTELY SURE you wish to take this action?  This is "
	    "your LAST CHANCE to cancel!"),
	    slice_get_number(storage_get_selected_slice(a->s)),
	    slice_get_desc(storage_get_selected_slice(a->s)),
	    disk_get_desc(storage_get_selected_disk(a->s)))) {
		/* XXX check to make sure this slice is not mounted first */
		cmds = commands_new();
		command_add(cmds, "%s%s if=/dev/zero of=/dev/%s bs=32k count=16",
		    a->os_root, cmd_name(a, "DD"),
		    slice_get_device_name(storage_get_selected_slice(a->s)));
		if (commands_execute(a, cmds)) {
			inform(a->c, _("Start of primary partition was successfully wiped."));
		} else {
			inform(a->c, _("Some errors occurred. "
			    "Start of primary partition was not successfully wiped."));
		}
		commands_free(cmds);
	}
}

static void
ask_to_wipe_boot_sector(struct i_fn_args *a, struct commands *fcmds)
{
	struct commands *cmds;
	struct command *cmd;
	char *disk;

	for (cmd = command_get_first(fcmds); cmd != NULL;
	     cmd = command_get_next(cmd)) {
		disk = command_get_tag(cmd);
		if (disk != NULL &&
		    command_get_result(cmd) > 0 &&
		    command_get_result(cmd) < 256) {
			switch (dfui_be_present_dialog(a->c,
			    _("Bootblock Install Failed"),
			    _("Re-Initialize Bootblock|Cancel"),
			    _("Warning: bootblocks were not successfully "
			    "installed on the disk `%s'. This may be "
			    "because the disk is new and not yet "
			    "formatted. If this is the case, it might "
			    "help to re-initialize the boot sector, "
			    "then try installing the bootblock again. "
			    "Note that this should not affect the "
			    "partition table of the disk."),
			    disk)) {
			case 1:
				cmds = commands_new();
				command_add(cmds,
				    "%s%s | %s%s -B /dev/%s",
				    a->os_root, cmd_name(a, "YES"),
				    a->os_root, cmd_name(a, "FDISK"),
				    disk);
				if (commands_execute(a, cmds)) {
					inform(a->c, _("Boot sector successfully initialized."));
				} else {
					inform(a->c, _("Some errors occurred. "
					    "Boot sector was not successfully initialized."));
				}
				commands_free(cmds);
				break;
			default:
				break;
			}
		}
	}
}

void
fn_install_bootblocks(struct i_fn_args *a, const char *device)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *ds;
	struct disk *d;
	struct commands *cmds;
	struct command *cmd;
	char disk[64], boot0cfg[32], packet[32];
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    "'Packet Mode' refers to using newer BIOS calls to boot "
	    "from a partition of the disk.  It is generally not "
	    "required unless:\n\n"
	    "- your BIOS does not support legacy mode; or\n"
	    "- your %s primary partition resides on a "
	    "cylinder of the disk beyond cylinder 1024; or\n"
	    "- you just can't get it to boot without it.",
	    OPERATING_SYSTEM_NAME);

	f = dfui_form_create(
	    "install_bootstrap",
	    _("Install Bootblock(s)"),
	    a->short_desc,

	    msg_buf[0],

	    "p", "special", "dfinstaller_install_bootstrap",

	    "f", "disk", _("Disk Drive"),
	    _("The disk on which you wish to install a bootblock"), "",
	    "p", "editable", "false",
	    "f", "boot0cfg", _("Install Bootblock?"),
	    _("Install a bootblock on this disk"), "",
	    "p", "control", "checkbox",
	    "f", "packet", _("Packet Mode?"),
	    _("Select this to use 'packet mode' to boot the disk"), "",
	    "p", "control", "checkbox",

	    "a", "ok", _("Accept and Install Bootblocks"), "", "",
	    "a", "cancel", a->cancel_desc, "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	dfui_form_set_multiple(f, 1);

	if (device != NULL) {
		ds = dfui_dataset_new();
		dfui_dataset_celldata_add(ds, "disk", device);
		dfui_dataset_celldata_add(ds, "boot0cfg", "Y");
		dfui_dataset_celldata_add(ds, "packet", "Y");
		dfui_form_dataset_add(f, ds);
	} else {
		for (d = storage_disk_first(a->s); d != NULL; d = disk_next(d)) {
			ds = dfui_dataset_new();
			dfui_dataset_celldata_add(ds, "disk",
			    disk_get_device_name(d));
			dfui_dataset_celldata_add(ds, "boot0cfg", "Y");
			dfui_dataset_celldata_add(ds, "packet", "Y");
			dfui_form_dataset_add(f, ds);
		}
	}

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	a->result = 0;
	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		cmds = commands_new();

		for (ds = dfui_response_dataset_get_first(r); ds != NULL;
		     ds = dfui_dataset_get_next(ds)) {
			strlcpy(disk, dfui_dataset_get_value(ds, "disk"), 64);
			strlcpy(boot0cfg, dfui_dataset_get_value(ds, "boot0cfg"), 32);
			strlcpy(packet, dfui_dataset_get_value(ds, "packet"), 32);

			if (strcasecmp(boot0cfg, "Y") == 0) {
				cmd = command_add(cmds, "%s%s -B -o %spacket %s",
				    a->os_root, cmd_name(a, "BOOT0CFG"),
				    strcasecmp(packet, "Y") == 0 ? "" : "no",
				    disk);
				command_set_failure_mode(cmd, COMMAND_FAILURE_WARN);
				command_set_tag(cmd, "%s", disk);
				cmd = command_add(cmds, "%s%s -v %s",
				    a->os_root, cmd_name(a, "BOOT0CFG"),
				    disk);
				command_set_failure_mode(cmd, COMMAND_FAILURE_WARN);
				command_set_tag(cmd, "%s", disk);
			}
		}

		if (!commands_execute(a, cmds)) {
			ask_to_wipe_boot_sector(a, cmds);
		} else {
			inform(a->c, _("Bootblocks were successfully installed!"));
			a->result = 1;
		}
		commands_free(cmds);
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

void
fn_format_msdos_floppy(struct i_fn_args *a)
{
	struct commands *cmds;

	switch (dfui_be_present_dialog(a->c, _("Format MSDOS Floppy"),
	    _("Format Floppy|Return to Utilities Menu"),
	    _("Please insert the floppy to be formatted "
	    "in unit 0 (``drive A:'')."))) {
	case 1:
		cmds = commands_new();
		command_add(cmds, "%s%s -y -f 1440 /dev/fd0",
		    a->os_root, cmd_name(a, "FDFORMAT"));
		command_add(cmds, "%s%s -f 1440 fd0",
		    a->os_root, cmd_name(a, "NEWFS_MSDOS"));
		if (commands_execute(a, cmds))
			inform(a->c, _("Floppy successfully formatted!"));
		else
			inform(a->c, _("Floppy was not successfully formatted."));
		break;
	case 2:
		return;
	default:
		abort_backend();
	}
}

void
fn_create_cdboot_floppy(struct i_fn_args *a)
{
	struct commands *cmds;
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    "%s cannot be installed from a floppy; "
	    "it must be installed from a booted CD-ROM. "
	    "However, many older systems do not support booting "
	    "from a CD-ROM. For these systems, a boot disk can be "
	    "created. This boot disk contains the Smart Boot "
	    "Manager program, which can boot a CD-ROM even "
	    "on systems with BIOSes which do not support booting "
	    "from the CD-ROM.\n\n"
	    "Smart Boot Manager is not a part of %s; "
	    "the Smart Boot Manager project can be found here:\n\n"
	    "http://btmgr.sourceforge.net/\n\n"
	    "To create a CDBoot floppy, insert a blank floppy "
	    "in unit 0 (``drive A:'') before proceeding."
	    "",
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME);

	switch (dfui_be_present_dialog(a->c, _("Create CDBoot Floppy"),
	    _("Create CDBoot Floppy|Return to Utilities Menu"),
	    "%s", msg_buf[0])) {
	case 1:
		cmds = commands_new();
		command_add(cmds, "%s%s -c %sboot/cdboot.flp.bz2 | "
		    "%s%s of=/dev/fd0 bs=32k",
		    a->os_root, cmd_name(a, "BUNZIP2"),
		    a->os_root,
		    a->os_root, cmd_name(a, "DD"));
		if (commands_execute(a, cmds))
			inform(a->c, _("CDBoot floppy successfully created!"));
		else
			inform(a->c, _("CDBoot floppy was not successfully created."));
		break;
	case 2:
		return;
	default:
		abort_backend();
	}
}

/**** NON-fn_ FUNCTIONS ***/

int
format_slice(struct i_fn_args *a)
{
	struct commands *cmds;
	struct command *cmd;
	int result;
	int cyl, hd, sec;

	cmds = commands_new();

	/*
	 * The information in a->s NEEDS to be accurate here!
	 * Presumably we just did a survey_storage() recently.
	 * XXX should we do another one here anyway just to be paranoid?
	 */

	/*
	 * Make sure the survey did get disk info correctly or fail
	 */
	if ((storage_get_selected_disk(a->s) == NULL) ||
	    (storage_get_selected_slice(a->s) == NULL))
		return 0;

	/*
	 * Set the slice's sysid to 165.
	 */
	disk_get_geometry(storage_get_selected_disk(a->s), &cyl, &hd, &sec);
	command_add(cmds, "%s%s 'g c%d h%d s%d' >%snew.fdisk",
	    a->os_root, cmd_name(a, "ECHO"),
	    cyl, hd, sec,
	    a->tmp);
	command_add(cmds, "%s%s 'p %d %d %lu %lu' >>%snew.fdisk",
	    a->os_root, cmd_name(a, "ECHO"),
	    slice_get_number(storage_get_selected_slice(a->s)),
	    165,
	    slice_get_start(storage_get_selected_slice(a->s)),
	    slice_get_size(storage_get_selected_slice(a->s)),
	    a->tmp);
	if (slice_get_flags(storage_get_selected_slice(a->s)) & 0x80) {
		command_add(cmds, "%s%s 'a %d' >>%snew.fdisk",
		    a->os_root, cmd_name(a, "ECHO"),
		    slice_get_number(storage_get_selected_slice(a->s)),
		    a->tmp);
	}

	command_add(cmds, "%s%s %snew.fdisk",
	    a->os_root, cmd_name(a, "CAT"), a->tmp);
	temp_file_add(a, "new.fdisk");

	/*
	 * Execute the fdisk script.
	 */
	cmd = command_add(cmds, "%s%s -v -f %snew.fdisk %s",
	    a->os_root, cmd_name(a, "FDISK"), a->tmp,
	    disk_get_device_name(storage_get_selected_disk(a->s)));
	if (slice_get_size(storage_get_selected_slice(a->s)) == 0xFFFFFFFFU)
		command_set_failure_mode(cmd, COMMAND_FAILURE_IGNORE);

	/*
	 * If there is an old 'virgin' disklabel hanging around
	 * in the temp dir, get rid of it.  This won't happen
	 * from a real CD, but might happen with '-o' installs.
	 */
	command_add(cmds, "%s%s -f %sinstall.disklabel.%s",
	    a->os_root, cmd_name(a, "RM"),
	    a->tmp,
	    slice_get_device_name(storage_get_selected_slice(a->s)));

	result = commands_execute(a, cmds);

	commands_free(cmds);

	return(result);
}
