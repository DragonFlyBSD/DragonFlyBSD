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
 * flow.c
 * Workflow logic for installer.
 * $Id: flow.c,v 1.67 2005/04/08 08:09:23 cpressey Exp $
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#include "libdfui/lang.h"
#define _(String) gettext (String)
extern int _nl_msg_cat_cntr;
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/dict.h"
#include "libaura/fspred.h"

#include "libdfui/dfui.h"
#ifdef DEBUG
#include "libdfui/dump.h"
#endif
#include "libdfui/system.h"

#include "libinstaller/commands.h"
#include "libinstaller/confed.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/package.h"
#include "libinstaller/uiutil.h"

#include "flow.h"
#include "fn.h"
#include "pathnames.h"

/*** GLOBALS ***/

void (*state)(struct i_fn_args *) = NULL;
int do_reboot;

/*** STATES ***/

/*
 * The installer works like a big state machine.  Each major form is
 * a state.  When the user has filled out the form satisfactorily,
 * and selects "OK", there is a transition to the next state, in a
 * mostly-linear order towards the final, "successfully installed"
 * state.  The user may also "Cancel", which generally causes a
 * transition to the previous state (but may also take them back to
 * the very first state in some cases.)
 *
 * Installer States:
 * - Select localization	optional
 * - Welcome to DragonFly	required
 * - Begin Installation		required
 * - Select Disk		required
 * - Format Disk		optional	dd, fdisk
 * - Select Partition		required	dd, disklabel
 * - Create Subpartitions	required	disklabel, newfs
 * - Install DragonFly		required	swapon, mkdir, mount, cpdup
 * - Install Bootstrap		optional	boot0cfg
 * - Reboot			optional	reboot
 */

#ifdef ENABLE_NLS
void
state_lang_menu(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	int done = 0;
	char *id;
	int cancelled = 0;

	while (!done) {
		f = dfui_form_create(
			"main_menu",
			_("Select Language"),
			_("Please select the language you wish you use."),
			"",

			"p", "role", "menu",

			"a", "default", "English",
			"English Standard Default", "",
			"a", "ru", "Russian",
			"Russian KOI8-R", "",
			NULL
		);

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		id = aura_strdup(dfui_response_get_action_id(r));

		if (strcmp(id, "default") == 0) {
			state = state_welcome;
			return;
		} else {
			state = state_welcome;
			done = 1;
		}

		dfui_form_free(f);
		dfui_response_free(r);
	}

	/* set keymap, scrnmap, fonts */
	if (!set_lang_syscons(id))
		return;

	/* set envars */
	if (!set_lang_envars(id))
		return;

	dfui_be_set_global_setting(a->c, "lang", id, &cancelled);

	/* XXX if (!cancelled) ... ? */

	/* let gettext know about changes */
	++_nl_msg_cat_cntr;
}
#endif

/*
 * state_welcome_livecd: the start state of the installer state machine,
 * when run from the Live CD.  Briefly describe DragonFly to the user,
 * and present them with a set of reasonable options of how to proceed.
 */
void
state_welcome(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	char msg_buf[2][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Welcome to %s"), OPERATING_SYSTEM_NAME);

	snprintf(msg_buf[1], sizeof(msg_buf[1]),
	    _("Welcome to the %s Live CD."
	    "\n\n"
	    "%s is an efficient and elegant BSD "
	    "Unix-derived operating system.  For more information, see %s"
	    "\n\n"
	    "From this CD, you can boot into %s ``live'' "
	    "(without installing it) to evaluate it, to install it "
	    "manually, or to troubleshoot problems with an "
	    "existing installation, using either a command prompt "
	    "or menu-driven utilities."
	    "\n\n"
	    "Also, you can use this automated application to assist "
	    "you in installing %s on this computer and "
	    "configuring it once it is installed."
	    ""),
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_URL,
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME);

	if ((a->flags & I_BOOTED_LIVECD) == 0) {
		state = state_welcome_system;
		return;
	}

	f = dfui_form_create(
	    "welcome",
	    msg_buf[0],

	    msg_buf[1],

	    "",

	    "p",	"special", 	"dfinstaller_welcome",

	    NULL
	);

	if (a->flags & I_UPGRADE_TOOGLE) {
		snprintf(msg_buf[0], sizeof(msg_buf[0]),
		    _("Upgrade a FreeBSD 4.X system to %s"),
		    OPERATING_SYSTEM_NAME);
		dfui_form_action_add(f, "upgrade",
		    dfui_info_new(_("Upgrade"),
		    msg_buf[0], ""));
	} else {
		snprintf(msg_buf[0], sizeof(msg_buf[0]),
		    _("Install %s"), OPERATING_SYSTEM_NAME);
		snprintf(msg_buf[1], sizeof(msg_buf[1]),
		    _("Install %s on a HDD or HDD partition on this computer"),
		    OPERATING_SYSTEM_NAME);
		dfui_form_action_add(f, "install",
		    dfui_info_new(msg_buf[0],
		    msg_buf[1], ""));
	}

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Configure a %s system once it has been installed on HDD"),
	    OPERATING_SYSTEM_NAME);
	dfui_form_action_add(f, "configure",
	    dfui_info_new(_("Configure an Installed System"),
	    msg_buf[0], ""));

	dfui_form_action_add(f, "utilities",
	    dfui_info_new(_("Live CD Utilities"),
	    _("Utilities to work with disks, diagnostics, and the LiveCD Environment"), ""));

	dfui_form_action_add(f, "exit",
	    dfui_info_new(_("Exit to Live CD"),
	    _("Exit this program to a login prompt with access to the LiveCD"), ""));

	dfui_form_action_add(f, "reboot",
	    dfui_info_new(_("Reboot this Computer"),
	    _("Reboot this computer (e.g. to boot into a newly installed system)"), ""));

	dfui_form_action_add(f, "configure_netboot",
	    dfui_info_new(_("Setup NetBoot Install Services"),
	    _("Setup machine as remote installation server"), ""));

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "install") == 0) {
		state = state_begin_install;
	} else if (strcmp(dfui_response_get_action_id(r), "upgrade") == 0) {
		state = state_begin_upgrade;
	} else if (strcmp(dfui_response_get_action_id(r), "configure") == 0) {
		storage_set_selected_disk(a->s, NULL);
		storage_set_selected_slice(a->s, NULL);
		state = state_configure_menu;
	} else if (strcmp(dfui_response_get_action_id(r), "utilities") == 0) {
		state = state_utilities_menu;
	} else if (strcmp(dfui_response_get_action_id(r), "exit") == 0) {
		state = NULL;
        } else if (strcmp(dfui_response_get_action_id(r), "configure_netboot") == 0) {
                state = state_setup_remote_installation_server;
	} else if (strcmp(dfui_response_get_action_id(r), "reboot") == 0) {
		state = state_reboot;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 * state_welcome_system: the start state of the installer state machine,
 * when run from the installed system.  Allow the user to configure the
 * system.
 */
void
state_welcome_system(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	char msg_buf[2][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Configure this %s System"), OPERATING_SYSTEM_NAME);

	snprintf(msg_buf[1], sizeof(msg_buf[1]),
	    _("Thank you for choosing %s."
	    "\n\n"
	    "For up-to-date news and information on %s, "
	    "make sure to check out"
	    "\n\n"
	    "%s"
	    "\n\n"
	    "You can use this automated application to assist "
	    "you in setting up this %s system."
	    ""),
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME,
	    OPERATING_SYSTEM_URL, OPERATING_SYSTEM_NAME);


	f = dfui_form_create(
	    "welcome",
	    msg_buf[0],

	    msg_buf[1],

	    "",

	    "p",	"special", 	"dfinstaller_welcome",

	    NULL
	);

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Configure this %s system"), OPERATING_SYSTEM_NAME);

	dfui_form_action_add(f, "environment",
	    dfui_info_new(_("Configure this System"),
	    msg_buf[0], ""));

	dfui_form_action_add(f, "utilities",
	    dfui_info_new(_("Utilities"),
	    _("Utilities to work with and diagnose disks and other subsystems"), ""));

	dfui_form_action_add(f, "exit",
	    dfui_info_new(_("Exit Installer"),
	    _("Exit this program and return to the system"), ""));

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "environment") == 0) {
		state = state_environment_menu;
	} else if (strcmp(dfui_response_get_action_id(r), "utilities") == 0) {
		state = state_utilities_menu;
	} else if (strcmp(dfui_response_get_action_id(r), "exit") == 0) {
		state = NULL;
	} else if (strcmp(dfui_response_get_action_id(r), "reboot") == 0) {
		state = state_reboot;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

void
state_configure_menu(struct i_fn_args *a)
{
	struct dfui_form *f = NULL;
	struct dfui_response *r = NULL;
	struct commands *cmds;
	int done = 0;
	char msg_buf[2][1024];

	if (storage_get_selected_disk(a->s) == NULL || storage_get_selected_slice(a->s) == NULL) {
		if (!survey_storage(a)) {
			inform(a->c, _("Errors occurred while probing "
			    "the system for its storage capabilities."));
		}

		a->short_desc = _("Select the disk containing the installation.");
		a->cancel_desc = _("Return to Welcome Menu");
		fn_select_disk(a);
		if (!a->result || storage_get_selected_disk(a->s) == NULL) {
			state = state_welcome;
			return;
		}

		a->short_desc = _("Select the primary partition containing the installation.");
		a->cancel_desc = _("Return to Welcome Menu");
		fn_select_slice(a);

		if (!a->result || storage_get_selected_slice(a->s) == NULL) {
			state = state_welcome;
			return;
		}
	}

	a->cfg_root = "mnt";

	if (during_install == 0) {
		switch (dfui_be_present_dialog(a->c, _("Select file system"),
		    _("HAMMER|UFS|Return to Welcome Menu"),
		    _("Please select the file system installed on the disk.\n\n")))
		{
		case 1:
			/* HAMMER */
			use_hammer = 1;
			break;
		case 2:
			/* UFS */
			use_hammer = 0;
			break;
		case 3:
			state = state_welcome;
			return;
			/* NOTREACHED */
			break;
		default:
			abort_backend();
			break;
		}
	}

	if (!mount_target_system(a)) {
		inform(a->c, _("Target system could not be mounted."));
		state = state_welcome;
		return;
	}

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("The options on this menu allow you to configure a "
	    "%s system after it has already been "
	    "installed."), OPERATING_SYSTEM_NAME);

	while (!done) {
		f = dfui_form_create(
		    "configure_menu",
		    _("Configure an Installed System"),
		    msg_buf[0],
		    "",
		    "p", "role", "menu",

		    "a", "set_timezone",
		    _("Select timezone"),
		    _("Set the Time Zone of your physical location"), "",
		    "a", "set_datetime",
		    _("Set date and time"),
		    _("Set the Time and Date of your machine"), "",

		    "a", "set_kbdmap",
		    _("Set keyboard map"),
		    _("Set what kind of keyboard layout you have"), "",
		    "a", "root_passwd",	_("Set root password"),
		    _("Set the password that the root (superuser) account will use"), "",
		    "a", "add_user", _("Add a user"),
		    _("Add a user to the system"), "",
		    "a", "assign_ip", _("Configure network interfaces"),
		    _("Set up network interfaces (NICs, ethernet, TCP/IP, etc)"), "",
		    "a", "assign_hostname_domain",
		    _("Configure hostname and domain"),
		    _("Configure the hostname and domain for this system"), "",
		    /*
		    "a", "select_services", "Select Services",
		    "Enable/Disable system services (servers, daemons, etc.)", "",
		    */
		    "a", "set_vidfont",
		    _("Set console font"),
		    _("Set how the characters on your video console look"), "",
		    "a", "set_scrnmap",
		    _("Set screen map"),
		    _("Set how characters are translated before console display"), "",
		    /*
		    "a", "install_pkgs", _("Install extra software packages"),
		    _("Install third-party software packages from the LiveCD"), "",
		    */
		    "a", "remove_pkgs",	_("Remove software packages"),
		    _("Remove third-party software packages from the installed system"), "",

		    "a", "cancel", _("Return to Welcome Menu"), "", "",
		    "p", "accelerator", "ESC",

		    NULL
		);

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		/* XXX set up a */
		a->cfg_root = "mnt/";
		if (strcmp(dfui_response_get_action_id(r), "root_passwd") == 0) {
			fn_root_passwd(a);
		} else if (strcmp(dfui_response_get_action_id(r), "add_user") == 0) {
			fn_add_user(a);
		} else if (strcmp(dfui_response_get_action_id(r), "install_pkgs") == 0) {
			fn_install_packages(a);
		} else if (strcmp(dfui_response_get_action_id(r), "remove_pkgs") == 0) {
			fn_remove_packages(a);
		} else if (strcmp(dfui_response_get_action_id(r), "assign_ip") == 0) {
			fn_assign_ip(a);
		} else if (strcmp(dfui_response_get_action_id(r), "assign_hostname_domain") == 0) {
			fn_assign_hostname_domain(a);
		} else if (strcmp(dfui_response_get_action_id(r), "select_services") == 0) {
			fn_select_services(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_kbdmap") == 0) {
			fn_set_kbdmap(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_vidfont") == 0) {
			fn_set_vidfont(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_scrnmap") == 0) {
			fn_set_scrnmap(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_timezone") == 0) {
			fn_set_timezone(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_datetime") == 0) {
			fn_assign_datetime(a);
		} else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			state = state_welcome;
			done = 1;
		}

		dfui_form_free(f);
		dfui_response_free(r);
	}

	/*
	 * Before unmounting the system, write out any changes to rc.conf.
	 */
	config_vars_write(rc_conf, CONFIG_TYPE_SH,
	    "%s%setc/rc.conf", a->os_root, a->cfg_root);

	/*
	 * Clear out configuration variable table in memory.
	 */
	config_vars_free(rc_conf);
	rc_conf = config_vars_new();

	/*
	 * Finally, unmount the system we mounted on /mnt and remove mappings.
	 */
	cmds = commands_new();
	unmount_all_under(a, cmds, "%smnt", a->os_root);
	commands_execute(a, cmds);
	commands_free(cmds);

	if (remove_all_mappings(a) == NULL)
		inform(a->c, _("Warning: mappings could not be removed."));
}

void
state_utilities_menu(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;

	if (!survey_storage(a)) {
		inform(a->c, _("Errors occurred while probing "
		    "the system for its storage capabilities."));
	}

	f = dfui_form_create(
	    "utilities_menu",
	    _("Live CD Utilities Menu"),
	    _("On these submenus you will find utilities to help "
	    "you set up your Live CD environment, diagnose "
	    "and analyse this system, and work with "
	    "the devices attached to this computer."),
	    "",
	    "p", "role", "menu",
	    "a", "environment", _("LiveCD Environment"),
	    _("Configure the LiveCD Environment"), "",
	    "a", "diagnostics", _("System Diagnostics"),
	    _("Probe and display detailed information about this system"), "",
	    "a", "diskutil", _("Disk Utilities"),
	    _("Format and check hard drives and floppy disks"), "",
	    "a", "livecd", _("Exit to Live CD"),
	    _("Exit this program to a login prompt with access to the LiveCD"), "",
	    "a", "reboot",
	    _("Reboot this Computer"), "", "",
	    "a", "cancel",
	    _("Return to Welcome Menu"), "", "",
	    "p", "accelerator", "ESC",
	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "environment") == 0)
		state = state_environment_menu;
	else if (strcmp(dfui_response_get_action_id(r), "diagnostics") == 0)
		state = state_diagnostics_menu;
	else if (strcmp(dfui_response_get_action_id(r), "diskutil") == 0)
		state = state_diskutil_menu;
	else if (strcmp(dfui_response_get_action_id(r), "livecd") == 0)
		state = NULL;
	else if (strcmp(dfui_response_get_action_id(r), "reboot") == 0)
		state = state_reboot;
	else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0)
		state = state_welcome;

	dfui_form_free(f);
	dfui_response_free(r);
}

void
state_environment_menu(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	int done = 0;
	char msg_buf[2][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("On this menu you will find utilities to help you "
	    "set up your Live CD environment.\n\nNote "
	    "that these functions affect only the LiveCD "
	    "environment you are currently using, and they will "
	    "not affect any system that may be installed on "
	    "this computer UNLESS you subsequently choose to "
	    "install %s from this environment, in which "
	    "case they will be copied to the newly installed "
	    "system."), OPERATING_SYSTEM_NAME);

	while (!done) {
		f = dfui_form_create(
		    "environment_menu",
		    _("Live CD Environment Menu"),
		    msg_buf[0],
		    "",
		    "p", "role", "menu",

		    "a", "set_timezone",
		    _("Select timezone"),
		    _("Set the Time Zone of your physical location"), "",
		    "a", "set_datetime",
		    _("Set date and time"),
		    _("Set the Time and Date of your machine"), "",

		    "a", "set_kbdmap",
		    _("Set keyboard map"),
		    _("Set what kind of keyboard layout you have"), "",
		    "a", "set_vidfont",
		    _("Set console font"),
		    _("Set how the characters on your video console look"), "",
		    "a", "set_scrnmap",
		    _("Set screen map"),
		    _("Set how characters are translated before console display"), "",

		    "a", "assign_hostname_domain",
		    _("Configure hostname and domain"),
		    _("Configure the hostname and domain for this system"), "",
		    "a", "assign_ip",
		    _("Configure network interfaces"),
		    _("Set up network interfaces (NICs, ethernet, TCP/IP, etc)"), "",

		    "a", "cancel",
		    _("Return to Utilities Menu"), "", "",
		    "p", "accelerator", "ESC",

		    NULL
		);

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		/* Set up a */
		a->cfg_root = "";
		if (strcmp(dfui_response_get_action_id(r), "set_kbdmap") == 0) {
			fn_set_kbdmap(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_vidfont") == 0) {
			fn_set_vidfont(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_scrnmap") == 0) {
			fn_set_scrnmap(a);
		} else if (strcmp(dfui_response_get_action_id(r), "assign_hostname_domain") == 0) {
			fn_assign_hostname_domain(a);
		} else if (strcmp(dfui_response_get_action_id(r), "assign_ip") == 0) {
			fn_assign_ip(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_timezone") == 0) {
			fn_set_timezone(a);
		} else if (strcmp(dfui_response_get_action_id(r), "set_datetime") == 0) {
			fn_assign_datetime(a);
		} else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			state = state_utilities_menu;
			done = 1;
		}

		dfui_form_free(f);
		dfui_response_free(r);
	}
}

void
state_diagnostics_menu(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	int done = 0;

	while (!done) {
		f = dfui_form_create(
		    "utilities_menu",
		    _("Live CD Diagnostics Menu"),
		    _("These functions can help you diagnose this system."),
		    "",
		    "p", "role", "menu",

		    "a", "show_dmesg",
		    _("Display system startup messages"),
		    _("Display system startup messages (dmesg)"), "",
		    "a", "pciconf",
		    _("Display PCI devices"),
		    _("Display PCI devices (pciconf)"), "",
		    "a", "natacontrol",
		    _("Display ATA devices"),
		    _("Display ATA devices (natacontrol)"), "",
		    NULL
		);

		k = dfui_form_action_add(f, "cancel",
		    dfui_info_new(_("Return to Utilities Menu"), "", ""));
		dfui_action_property_set(k, "accelerator", "ESC");

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		/* XXX set up a */
		if (strcmp(dfui_response_get_action_id(r), "show_dmesg") == 0) {
			fn_show_dmesg(a);
		} else if (strcmp(dfui_response_get_action_id(r), "pciconf") == 0) {
			fn_show_pciconf(a);
		} else if (strcmp(dfui_response_get_action_id(r), "natacontrol") == 0) {
			fn_show_natacontrol(a);
		} else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			state = state_utilities_menu;
			done = 1;
		}

		dfui_form_free(f);
		dfui_response_free(r);
	}
}

void
state_diskutil_menu(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	int done = 0;

	while (!done) {
		f = dfui_form_create(
		    "utilities_menu",
		    _("Disk Utilities Menu"),
		    _("These functions let you manipulate the storage devices "
		    "attached to this computer."),
		    "",

		    "p", "role", "menu",

		    "a", "format_hdd",
		    _("Format a hard disk drive"), "", "",
		    "a", "wipe_start_of_disk",
		    _("Wipe out the start of a disk"), "", "",
		    "a", "wipe_start_of_slice",
		    _("Wipe out the start of a primary partition"), "", "",
		    "a", "install_bootblocks",
		    _("Install bootblocks on disks"), "", "",
		    "a", "format_msdos_floppy",
		    _("Format an MSDOS floppy"), "", "",
		    NULL
		);

		if (is_file("%sboot/cdboot.flp.bz2", a->os_root)) {
			dfui_form_action_add(f, "create_cdboot_floppy",
			    dfui_info_new(_("Create a CDBoot floppy"),
			    "",
			    ""));
		}

		k = dfui_form_action_add(f, "cancel",
		    dfui_info_new(_("Return to Utilities Menu"), "", ""));
		dfui_action_property_set(k, "accelerator", "ESC");

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		/* XXX set up a */
		if (strcmp(dfui_response_get_action_id(r), "format_hdd") == 0) {
			storage_set_selected_disk(a->s, NULL);
			storage_set_selected_slice(a->s, NULL);
			fn_format_disk(a);
		} else if (strcmp(dfui_response_get_action_id(r), "wipe_start_of_disk") == 0) {
			fn_wipe_start_of_disk(a);
		} else if (strcmp(dfui_response_get_action_id(r), "wipe_start_of_slice") == 0) {
			fn_wipe_start_of_slice(a);
		} else if (strcmp(dfui_response_get_action_id(r), "install_bootblocks") == 0) {
			a->short_desc = _("Select the disks on which "
			    "you wish to install bootblocks.");
			a->cancel_desc = _("Return to Utilities Menu");
			fn_install_bootblocks(a, NULL);
		} else if (strcmp(dfui_response_get_action_id(r), "format_msdos_floppy") == 0) {
			fn_format_msdos_floppy(a);
		} else if (strcmp(dfui_response_get_action_id(r), "create_cdboot_floppy") == 0) {
			fn_create_cdboot_floppy(a);
		} else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			state = state_utilities_menu;
			done = 1;
		}

		dfui_form_free(f);
		dfui_response_free(r);
	}
}

/** INSTALLER STATES **/

/*
 * state_begin_upgrade: Ask the user where the freebsd
 * 4.X install is and make sure its safe to proceed.
 *
 */
void
state_begin_upgrade(struct i_fn_args *a)
{
        //struct dfui_form *f = NULL;
        //struct dfui_response *r = NULL;
        //int done = 0;

        if (storage_get_selected_disk(a->s) == NULL || storage_get_selected_slice(a->s) == NULL) {
		if (!survey_storage(a)) {
			inform(a->c, _("Errors occurred while probing "
			    "the system for its storage capabilities."));
		}

                a->short_desc = _("Select the disk containing the installation that you would like to upgrade.");
                a->cancel_desc = _("Return to Welcome Menu");
                fn_select_disk(a);
                if (!a->result || storage_get_selected_disk(a->s) == NULL) {
                        state = state_welcome;
                        return;
                }

                a->short_desc = _("Select the primary partition containing the installation you would like to upgrade.");
                a->cancel_desc = _("Return to Welcome Menu");
                fn_select_slice(a);

                if (!a->result || storage_get_selected_slice(a->s) == NULL) {
                        state = state_welcome;
                        return;
                }
        }

        a->cfg_root = "mnt";
        if (!mount_target_system(a)) {
                inform(a->c, _("Target system could not be mounted."));
                state = state_welcome;
                return;
        }
}

/*
 * state_begin_install: Briefly describe the install process
 * to the user, and let them proceed (or not.)
 */
void
state_begin_install(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	char msg_buf[3][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("This application will install %s"
	    " on one of the hard disk drives attached to this computer. "
	    "It has been designed to make it easy to install "
	    "%s in the typical case. "
	    "If you have special requirements that are not addressed "
	    "by this installer, or if you have problems using it, you "
	    "are welcome to install %s manually. "
	    "To do so select Exit to Live CD, login as root, and follow "
	    "the instructions given in the file /README ."
	    "\n\n"
	    "NOTE! As with any installation process, YOU ARE "
	    "STRONGLY ENCOURAGED TO BACK UP ANY IMPORTANT DATA ON THIS "
	    "COMPUTER BEFORE PROCEEDING!"
	    ""),
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME,
	    OPERATING_SYSTEM_NAME);

	snprintf(msg_buf[1], sizeof(msg_buf[1]),
	    _("Some situations in which you might not wish to use this "
	    "installer are:\n\n"
	    "- you want to install %s onto a "
	    "logical/extended partition;\n"
	    "- you want to install %s "
	    "onto a ``dangerously dedicated'' disk; or\n"
	    "- you want full and utter control over the install process."
	    ""),
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME);

	snprintf(msg_buf[2], sizeof(msg_buf[2]),
	    _("Install %s"), OPERATING_SYSTEM_NAME);

	f = dfui_form_create(
	    "begin_install",
	    _("Begin Installation"),
	    msg_buf[0],

	    msg_buf[1],
	    "p", "special", "dfinstaller_begin_install",
	    "p", "minimum_width", "76",

	    "a", "proceed", msg_buf[2],
	    "", "",
	    "a", "cancel", _("Return to Welcome Menu"),
	    "", "",
	    "p", "accelerator", "ESC",
	    "a", "livecd", _("Exit to Live CD"),
	    "", "",
	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "proceed") == 0) {
		if (!survey_storage(a)) {
			inform(a->c, _("Errors occurred while probing "
			    "the system for its storage capabilities."));
		}
		state = state_select_disk;
	} else if (strcmp(dfui_response_get_action_id(r), "livecd") == 0) {
		state = NULL;
	} else if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		state = state_welcome;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 * state_select_disk: ask the user on which physical disk they wish
 * to install DragonFly.
 */
void
state_select_disk(struct i_fn_args *a)
{
	struct disk *d;
	int num_disks = 0;
	char msg_buf[1][1024];

	for (d = storage_disk_first(a->s); d != NULL; d = disk_next(d))
		num_disks++;

	if (num_disks == 0) {
		inform(a->c, _("The installer could not find any disks suitable "
		    "for installation (IDE or SCSI) attached to this "
		    "computer.  If you wish to install %s"
		    " on an unorthodox storage device, you will have to "
		    "exit to a LiveCD command prompt and install it "
		    "manually, using the file /README as a guide."),
		    OPERATING_SYSTEM_NAME);
		state = state_welcome;
		return;
	}

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Select a disk on which to install %s"),
	    OPERATING_SYSTEM_NAME);
	a->short_desc = msg_buf[0];
	a->cancel_desc = _("Return to Begin Installation");
	fn_select_disk(a);
	if (!a->result || storage_get_selected_disk(a->s) == NULL) {
		state = state_begin_install;
	} else {
#if 0
		if (disk_get_capacity(storage_get_selected_disk(a->s)) < DISK_MIN) {
			inform(a->c, _("WARNING: you should have a disk "
			    "at least %dM in size, or "
			    "you may encounter problems trying to "
			    "install %s."), DISK_MIN, OPERATING_SYSTEM_NAME);
		}
#endif
		state = state_format_disk;
	}
}

void
state_ask_fs(struct i_fn_args *a)
{
	use_hammer = 0;

	switch (dfui_be_present_dialog(a->c, _("Select file system"),
	    _("Use HAMMER|Use UFS|Return to Select Disk"),
	    _("Please select the file system you want to use with %s.\n\n"
	      "HAMMER is the new %s file system.  UFS is the traditional BSD file system."),
	    OPERATING_SYSTEM_NAME,
	    OPERATING_SYSTEM_NAME))
	{
	case 1:
		/* HAMMER */
		use_hammer = 1;
		break;
	case 2:
		/* UFS */
		break;
	case 3:
		state = state_select_disk;
		return;
		/* NOTREACHED */
		break;
	default:
		abort_backend();
		break;
	}
	state = state_create_subpartitions;
}

/*
 * state_format_disk: ask the user if they wish to format the disk they
 * selected.
 */
void
state_format_disk(struct i_fn_args *a)
{
	switch (dfui_be_present_dialog(a->c, _("How Much Disk?"),
	    _("Use Entire Disk|Use Part of Disk|Return to Select Disk"),
	    _("Select how much of this disk you want to use for %s.\n\n%s"),
	    OPERATING_SYSTEM_NAME,
	    disk_get_desc(storage_get_selected_disk(a->s)))) {
	case 1:
		/* Entire Disk */
		if (measure_activated_swap_from_disk(a, storage_get_selected_disk(a->s)) > 0) {
			if (swapoff_all(a) == NULL) {
				inform(a->c, _("Warning: swap could not be turned off."));
				state = state_select_disk;
				return;
			}
		}

		fn_format_disk(a);
		if (a->result)
			state = state_ask_fs;
		else
			state = state_format_disk;
		break;
	case 2:
		/* Part of Disk */
		state = state_select_slice;
		break;
	case 3:
		/* Return */
		state = state_select_disk;
		break;
	default:
		abort_backend();
		break;
	}
}

/*
 * state_select_slice: ask the user which slice they wish to install
 * DragonFly on.  In order to avoid confusing them, refer to it as
 * a primary partition, but tell them what BSD has traditionally called
 * it, too.
 */
void
state_select_slice(struct i_fn_args *a)
{
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Select the existing primary partition (also "
	    "known as a `slice' in the BSD tradition) on "
	    "which to install %s.\n\n"
	    "Note that if you do not have any existing "
	    "primary partitions on this disk, you must "
	    "first create some. This installer does not "
	    "currently have the ability to do this, so "
	    "you will have to exit and run fdisk (in "
	    "DOS or *BSD) or parted (in Linux) to do so."),
	    OPERATING_SYSTEM_NAME);

	a->short_desc = msg_buf[0];
	a->cancel_desc = _("Return to Select Disk");
	fn_select_slice(a);
	if (!a->result || storage_get_selected_slice(a->s) == NULL) {
		state = state_select_disk;
	} else {
		if (measure_activated_swap_from_slice(a, storage_get_selected_disk(a->s),
		    storage_get_selected_slice(a->s)) > 0) {
			if (swapoff_all(a) == NULL) {
				inform(a->c, _("Warning: swap could not be turned off."));
				state = state_select_slice;
				return;
			}
		}

		if (slice_get_capacity(storage_get_selected_slice(a->s)) < DISK_MIN) {
			inform(a->c, _("WARNING: you should have a primary "
			    "partition at least %dM in size, or "
			    "you may encounter problems trying to "
			    "install %s."), DISK_MIN, OPERATING_SYSTEM_NAME);
		}

		if (confirm_dangerous_action(a->c,
		    _("WARNING!  ALL data in primary partition #%d,\n\n%s\n\non the "
		    "disk\n\n%s\n\n will be IRREVOCABLY ERASED!\n\nAre you "
		    "ABSOLUTELY SURE you wish to take this action?  This is "
		    "your LAST CHANCE to cancel!"),
		    slice_get_number(storage_get_selected_slice(a->s)),
		    slice_get_desc(storage_get_selected_slice(a->s)),
		    disk_get_desc(storage_get_selected_disk(a->s)))) {
			if (!format_slice(a)) {
				inform(a->c, _("Primary partition #%d was "
				    "not correctly formatted, and may "
				    "now be in an inconsistent state. "
				    "We recommend re-formatting it "
				    "before proceeding."),
				    slice_get_number(storage_get_selected_slice(a->s)));
			} else {
				inform(a->c, _("Primary partition #%d was formatted."),
				    slice_get_number(storage_get_selected_slice(a->s)));
				state = state_ask_fs;
			}
		} else {
			inform(a->c, _("Action cancelled - no primary partitions were formatted."));
			state = state_select_slice;
		}
	}
}

/*
 * state_create_subpartitions: let the user specify what subpartitions they
 * want on the disk, how large each should be, and where it should be mounted.
 */
void
state_create_subpartitions(struct i_fn_args *a)
{
	struct commands *cmds;

	if (measure_activated_swap_from_slice(a, storage_get_selected_disk(a->s),
	    storage_get_selected_slice(a->s)) > 0) {
		if (swapoff_all(a) == NULL) {
			inform(a->c, _("Warning: swap could not be turned off."));
			state = disk_get_formatted(storage_get_selected_disk(a->s)) ?
			    state_select_disk : state_select_slice;
			return;
		}
	}

	cmds = commands_new();

	/*
	 * Auto-disklabel the slice.
	 * NB: one cannot use "/dev/adXsY" here -
	 * it must be in the form "adXsY".
	 */
	command_add(cmds, "%s%s -W %s",
	    a->os_root, cmd_name(a, "DISKLABEL64"),
	    slice_get_device_name(storage_get_selected_slice(a->s)));
	command_add(cmds, "%s%s if=/dev/zero of=/dev/%s bs=32k count=16",
	    a->os_root, cmd_name(a, "DD"),
	    slice_get_device_name(storage_get_selected_slice(a->s)));
	command_add(cmds, "%s%s -B -r -w %s auto",
	    a->os_root, cmd_name(a, "DISKLABEL64"),
	    slice_get_device_name(storage_get_selected_slice(a->s)));
	commands_execute(a, cmds);
	commands_free(cmds);

	if (use_hammer)
		fn_create_subpartitions_hammer(a);
	else
		fn_create_subpartitions_ufs(a);

	if (a->result) {
		state = state_install_os;
	} else {
		state = disk_get_formatted(storage_get_selected_disk(a->s)) ?
		    state_select_disk : state_select_slice;
	}
}

/*
 * state_install_os: actually put DragonFly on the disk.
 */
void
state_install_os(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Everything is now ready to install the actual files which "
	    "comprise the %s operating system "
	    "on the selected partition of the selected disk.\n\n"
	    "Note that this process will take quite a while to finish. "
	    "You may wish to take a break now and come back to the "
	    "computer in a short while."),
	    OPERATING_SYSTEM_NAME);

	f = dfui_form_create(
	    "install_os",
	    _("Install OS"),
	    msg_buf[0],

	    "",

	    "p", "role", "confirm",
	    "p", "special", "dfinstaller_install_os",

	    "a", "ok", _("Begin Installing Files"), "", "",
	    "a", "cancel", _("Return to Create Subpartitions"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		state = state_create_subpartitions;
	} else {
		fn_install_os(a);
		if (a->result)
			state = state_install_bootstrap;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 * state_install_bootstrap: put boot0 bootblocks on selected disks.
 */
void
state_install_bootstrap(struct i_fn_args *a)
{
	char msg_buf[1][1024];

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("You may now wish to install bootblocks on one or more disks. "
	    "If you already have a boot manager installed, you can skip "
	    "this step (but you may have to configure your boot manager "
	    "separately.)  If you installed %s on a disk other "
	    "than your first disk, you will need to put the bootblock "
	    "on at least your first disk and the %s disk."),
	    OPERATING_SYSTEM_NAME, OPERATING_SYSTEM_NAME);

	a->short_desc = msg_buf[0];
	a->cancel_desc = _("Skip this Step");
	fn_install_bootblocks(a,
	    disk_get_device_name(storage_get_selected_disk(a->s)));
	state = state_finish_install;
}

/*
 * Finish up the install.
 */
void
state_finish_install(struct i_fn_args *a)
{
	char msg_buf[1][1024];
	during_install = 1;

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    "%s is Installed!",
	    OPERATING_SYSTEM_NAME);

	switch (dfui_be_present_dialog(a->c, msg_buf[0],
	    _("Configure this System|Reboot|Return to Welcome Menu"),
	    _("Congratulations!\n\n"
	    "%s has successfully been installed on "
	    "this computer. You may now proceed to configure "
	    "the installation. Alternately, you may wish to "
	    "reboot the computer and boot into the installed "
	    "system to confirm that it works."),
	    OPERATING_SYSTEM_NAME)) {
	case 1:
		state = state_configure_menu;
		break;
	case 2:
		state = state_reboot;
		break;
	case 3:
		state = state_welcome;
		break;
	default:
		abort_backend();
	}
}

/*
 * state_reboot: reboot the machine.
 */
void
state_reboot(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;

	f = dfui_form_create(
	    "reboot",
	    _("Reboot"),
	    _("This machine is about to be shut down. "
	    "After the machine has reached its shutdown state, "
	    "you may remove the CD from the CD-ROM drive tray "
	    "and press Enter to reboot from the HDD."),

	    "",

	    "p", "role", "confirm",

	    "a", "ok", _("Reboot"), "", "",
	    "a", "cancel", _("Return to Welcome Menu"), "", "",
	    "p", "accelerator", "ESC",
	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		state = state_welcome;
	} else {
		do_reboot = 1;
		state = NULL;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*
 *
 *  state_setup_remote_installation_server:
 *  Setup a remote boot installation environment where a machine
 *  can boot via DHCP/TFTP/NFS and have a running environment
 *  where the installer can setup the machine.
 *
 */
void
state_setup_remote_installation_server(struct i_fn_args *a)
{
        FILE *p;
        struct commands *cmds;
        struct dfui_form *f;
	struct dfui_action *k;
        struct dfui_response *r;
        char *word;
        char interface[256];
        char line[256];

        switch (dfui_be_present_dialog(a->c, _("Enable Netboot Installation Services?"),
            _("Enable NetBoot Installation Services|No thanks"),
            _("NetBoot Installation Services allows this machine to become "
            "a Installation Server that will allow the clients to boot over the network "
	    "via PXE and start the Installation Environment."
	    "\n\n*NOTE!*  This will assign the IP Address of 10.1.0.1/24 to the selected interface."
            "\n\nWould you like to provision this machine to serve up the LiveCD/Installer?"))) {
		case 1:
			/*
			 * Get interface list.
			 */
			p = popen("/sbin/ifconfig -l", "r");
			/* XXX it's possible (though extremely unlikely) this will fail. */
			while (fgets(line, 255, p) != NULL)
				line[strlen(line) - 1] = '\0';
			pclose(p);

			f = dfui_form_create(
			    "assign_ip",
			    _("Setup NetBoot Installation Environment"),
			    _("Please select which interface you would like to configure:"),
			    "",
			    "p",        "role", "menu",
			    NULL
			);

			/* Loop through array. */
			word = strtok(line, " \t");
			while (word != NULL) {
				dfui_form_action_add(f, word,
				    dfui_info_new(word, "", ""));
				word = strtok(NULL, " ");
			}

			k = dfui_form_action_add(f, "cancel",
			    dfui_info_new("Cancel", "", ""));
			dfui_action_property_set(k, "accelerator", "ESC");

			if (!dfui_be_present(a->c, f, &r))
				abort_backend();

			strlcpy(interface, dfui_response_get_action_id(r), 256);

			if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
				dfui_form_free(f);
				dfui_response_free(r);
				return;
			}

			/*
			 *
			 * Issues the necessary commands to setup the remote boot environment
			 *
			 */
			cmds = commands_new();
			command_add(cmds, "%s%s %s 10.1.0.1 netmask 255.255.255.0",
			    a->os_root, cmd_name(a, "IFCONFIG"), interface);
			command_add(cmds, "%s%s -p %stftpdroot",
			    a->os_root, cmd_name(a, "MKDIR"), a->tmp);
			command_add(cmds, "%s%s %sboot/pxeboot %stftpdroot",
			    a->os_root, cmd_name(a, "CP"), a->os_root, a->tmp);
			command_add(cmds, "%s%s %s -ro -alldirs -maproot=root: -network 10.1.0.0 -mask 255.255.255.0 >> %setc/exports",
			    a->os_root, cmd_name(a, "ECHO"), a->os_root, a->os_root);
			command_add(cmds, "%s%s tftp dgram udp wait root %s%s tftpd -l -s %stftpdroot >> %setc/inetd.conf",
			    a->os_root, cmd_name(a, "ECHO"),
			    a->os_root, cmd_name(a, "TFTPD"),
			    a->tmp, a->os_root);
			command_add(cmds, "%s%s",
			    a->os_root, cmd_name(a, "INETD"));
			command_add(cmds, "%s%s %svar/db/dhcpd.leases",
			    a->os_root, cmd_name(a, "TOUCH"), a->os_root);
			command_add(cmds, "%s%s -cf /etc/dhcpd.conf >/dev/null 2>&1",
			    a->os_root, cmd_name(a, "DHCPD"));
			command_add(cmds, "%s%s >/dev/null 2>&1",
			    a->os_root, cmd_name(a, "RPCBIND"));
			command_add(cmds, "%s%s -ln >/dev/null 2>&1",
			    a->os_root, cmd_name(a, "MOUNTD"));
			command_add(cmds, "%s%s -u -t -n 6 >/dev/null 2>&1",
			    a->os_root, cmd_name(a, "NFSD"));

			if (commands_execute(a, cmds)) {
				inform(a->c, _("NetBoot installation services are now started."));
			} else {
				inform(a->c, _("A failure occurred while provisioning the NetBoot environment.  Please check the logs."));
			}

			commands_free(cmds);
			dfui_form_free(f);
			dfui_response_free(r);

			break;
		case 2:

			break;

	}

	state = state_welcome;

}

/*** MAIN ***/

int
flow(int transport, char *rendezvous, char *os_root,
     int flags __unused)
{
	struct i_fn_args *a;

	rc_conf = config_vars_new();

	if ((a = i_fn_args_new(os_root, DEFAULT_INSTALLER_TEMP,
			       transport, rendezvous)) == NULL) {
		return(0);
	}

	/*
	 * XXX We can't handle this yet.
	 *
	   a->flags |= I_BOOTED_LIVECD;
	   a->flags |= I_UPGRADE_TOOGLE;
	*/
	a->flags |= I_BOOTED_LIVECD;

	/*
	 * Execute the state machine here.  The global function pointer
	 * variable `state' points to the next state_* function to execute.
	 * Before it exits, this function should set `state' to the next
	 * state to make a transition to, or NULL to indicate that the
	 * state machine is finished.
	 */
#ifdef ENABLE_NLS
	state = state_lang_menu;
#else
	state = state_welcome;
#endif
	for (; state != NULL; )
		state(a);

	config_vars_free(rc_conf);

	i_fn_args_free(a);

	return(do_reboot);
}
