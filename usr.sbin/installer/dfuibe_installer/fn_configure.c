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
 * fn_configure.c
 * Configuration functions for installer.
 * This includes both Configure the LiveCD Environment, and
 * Configure an Installed System (there is considerable overlap.)
 * $Id: fn_configure.c,v 1.82 2005/03/25 05:24:00 cpressey Exp $
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/dict.h"
#include "libaura/fspred.h"

#include "libdfui/dfui.h"
#include "libdfui/system.h"

#include "libinstaller/commands.h"
#include "libinstaller/confed.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/package.h"
#include "libinstaller/uiutil.h"

#include "fn.h"
#include "flow.h"
#include "pathnames.h"

static const char	*yes_to_y(const char *);
static char		*convert_tmpfs_options(char *);

/** CONFIGURE FUNCTIONS **/

#define	PW_NOT_ALLOWED		":;,`~!@#$%^&*()+={}[]\\|/?<>'\" "
#define	GECOS_NOT_ALLOWED	":,\\\""
#define	FILENAME_NOT_ALLOWED	":;`~!#$^&*()={}[]\\|?<>'\" "
#define	MEMBERSHIPS_NOT_ALLOWED	":;`~!@#$%^&*()+={}[]\\|/?<>'\" "

void
fn_add_user(struct i_fn_args *a)
{
	struct dfui_dataset *ds, *new_ds;
	struct dfui_form *f;
	struct dfui_response *r;
	struct commands *cmds;
	struct command *cmd;
	const char *username, *home, *passwd_1, *passwd_2, *gecos;
	const char *shell, *uid, *group, *groups;
	int done = 0;

	f = dfui_form_create(
	    "add_user",
	    _("Add user"),
	    _("Here you can add a user to an installed system.\n\n"
	    "You can leave the Home Directory, User ID, and Login Group "
	    "fields empty if you want these items to be automatically "
	    "allocated by the system.\n\n"
	    "Note: this user's password will appear in the install log. "
	    "If this is a problem, please add the user manually after "
	    "rebooting into the installed system instead."),
	    "",
	    "f", "username", _("Username"),
	    _("Enter the username the user will log in as"), "",
	    "f", "gecos", _("Real Name"),
	    _("Enter the real name (or GECOS field) of this user"), "",
	    "f", "passwd_1", _("Password"),
	    _("Enter the user's password (will not be displayed)"), "",
	    "p", "obscured", "true",
	    "f", "passwd_2", _("Password (Again)"),
	    _("Re-enter the user's password to confirm"), "",
	    "p", "obscured", "true",
	    "f", "shell", _("Shell"),
	    _("Enter the full path to the user's shell program"), "",
	    "f", "home", _("Home Directory"),
	    _("Enter the full path to the user's home directory, or leave blank"), "",
	    "f", "uid", _("User ID"),
	    _("Enter this account's numeric user id, or leave blank"), "",
	    "f", "group", _("Login Group"),
	    _("Enter the primary group for this account, or leave blank"), "",
	    "f", "groups", _("Other Group Memberships"),
	    _("Enter a comma-separated list of other groups "
	    "that this user should belong to"), "",
	    "a", "ok", _("Accept and Add"), "", "",
	    "a", "cancel", _("Return to Configure Menu"), "", "",
	    "p", "accelerator", "ESC",
	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "username", "");
	dfui_dataset_celldata_add(ds, "gecos", "");
	dfui_dataset_celldata_add(ds, "passwd_1", "");
	dfui_dataset_celldata_add(ds, "passwd_2", "");
	dfui_dataset_celldata_add(ds, "shell", "/bin/tcsh");
	dfui_dataset_celldata_add(ds, "home", "");
	dfui_dataset_celldata_add(ds, "uid", "");
	dfui_dataset_celldata_add(ds, "group", "");
	dfui_dataset_celldata_add(ds, "groups", "");
	dfui_form_dataset_add(f, ds);

	while (!done) {
		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
			done = 1;
			dfui_response_free(r);
			break;
		}

		new_ds = dfui_dataset_dup(dfui_response_dataset_get_first(r));
		dfui_form_datasets_free(f);
		dfui_form_dataset_add(f, new_ds);

		/* Fetch form field values. */

		username = dfui_dataset_get_value(new_ds, "username");
		home = dfui_dataset_get_value(new_ds, "home");
		gecos = dfui_dataset_get_value(new_ds, "gecos");
		shell = dfui_dataset_get_value(new_ds, "shell");
		passwd_1 = dfui_dataset_get_value(new_ds, "passwd_1");
		passwd_2 = dfui_dataset_get_value(new_ds, "passwd_2");
		uid = dfui_dataset_get_value(new_ds, "uid");
		group = dfui_dataset_get_value(new_ds, "group");
		groups = dfui_dataset_get_value(new_ds, "groups");

		if (strlen(username) == 0) {
			inform(a->c, _("You must enter a username."));
			done = 0;
		} else if (strcmp(passwd_1, passwd_2) != 0) {
			/* Passwords don't match; tell the user. */
			inform(a->c, _("The passwords do not match."));
			done = 0;
		} else if (!assert_clean(a->c, _("Username"), username, PW_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Real Name"), gecos, GECOS_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Password"), passwd_1, PW_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Shell"), shell, FILENAME_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Home Directory"), home, FILENAME_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("User ID"), uid, PW_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Login Group"), group, PW_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Group Memberships"), groups, MEMBERSHIPS_NOT_ALLOWED)) {
			done = 0;
		} else if (!is_program("%s%s", a->os_root, shell) &&
		    strcmp(shell, "/nonexistent") != 0) {
			inform(a->c, _("Chosen shell does not exist on the system."));
			done = 0;
		} else {
			cmds = commands_new();

			command_add(cmds, "%s%s %smnt/ /%s useradd "
			    "'%s' %s%s %s%s -c \"%s\" %s%s -s %s %s%s %s",
			    a->os_root, cmd_name(a, "CHROOT"),
			    a->os_root, cmd_name(a, "PW"),
			    username,
			    strlen(uid) == 0 ? "" : "-u ", uid,
			    strlen(group) == 0 ? "" : "-g ", group,
			    gecos,
			    strlen(home) == 0 ? "" : "-d ", home,
			    shell,
			    strlen(groups) == 0 ? "" : "-G ", groups,
			    (strlen(home) == 0 || !is_dir("%s", home)) ?
			    "-m -k /usr/share/skel" : "");

			cmd = command_add(cmds, "%s%s '%s' | "
			    "%s%s %smnt/ /%s usermod '%s' -h 0",
			    a->os_root, cmd_name(a, "ECHO"),
			    passwd_1,
			    a->os_root, cmd_name(a, "CHROOT"),
			    a->os_root, cmd_name(a, "PW"),
			    username);
			command_set_desc(cmd, _("Setting password..."));

			if (commands_execute(a, cmds)) {
				inform(a->c, _("User `%s' was added."), username);
				done = 1;
			} else {
				inform(a->c, _("User was not successfully added."));
				done = 0;
			}

			commands_free(cmds);
		}

		dfui_response_free(r);
	}

	dfui_form_free(f);
}

void
fn_root_passwd(struct i_fn_args *a)
{
	struct dfui_dataset *ds, *new_ds;
	struct dfui_form *f;
	struct dfui_response *r;
	struct commands *cmds;
	struct command *cmd;
	const char *root_passwd_1, *root_passwd_2;
	int done = 0;

	f = dfui_form_create(
	    "root_passwd",
	    _("Set Root Password"),
	    _("Here you can set the super-user (root) password.\n\n"
	    "Note: root's new password will appear in the install log. "
	    "If this is a problem, please set root's password manually "
	    "after rebooting into the installed system instead."),
	    "",

	    "f", "root_passwd_1", _("Root password"),
	    _("Enter the root password you would like to use"), "",
	    "p", "obscured", "true",
	    "f", "root_passwd_2", _("Root password again"),
	    _("Enter the root password again to confirm"), "",
	    "p", "obscured", "true",

	    "a", "ok", _("Accept and Set Password"), "", "",
	    "a", "cancel", _("Return to Configure Menu"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "root_passwd_1", "");
	dfui_dataset_celldata_add(ds, "root_passwd_2", "");
	dfui_form_dataset_add(f, ds);

	while (!done) {
		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
			new_ds = dfui_dataset_dup(dfui_response_dataset_get_first(r));
			dfui_form_datasets_free(f);
			dfui_form_dataset_add(f, new_ds);

			/*
			 * Fetch form field values.
			 */

			root_passwd_1 = dfui_dataset_get_value(new_ds, "root_passwd_1");
			root_passwd_2 = dfui_dataset_get_value(new_ds, "root_passwd_2");

			if (!assert_clean(a->c, _("Root password"), root_passwd_1, PW_NOT_ALLOWED)) {
				done = 0;
			} else if (strlen(root_passwd_1) == 0 && strlen(root_passwd_2) == 0) {
				done = 0;
			} else if (strcmp(root_passwd_1, root_passwd_2) == 0) {
				/*
				 * Passwords match, so set the root password.
				 */
				cmds = commands_new();
				cmd = command_add(cmds, "%s%s '%s' | "
				    "%s%s %smnt/ /%s usermod root -h 0",
				    a->os_root, cmd_name(a, "ECHO"),
				    root_passwd_1,
				    a->os_root, cmd_name(a, "CHROOT"),
				    a->os_root, cmd_name(a, "PW"));
				command_set_desc(cmd, _("Setting password..."));
				if (commands_execute(a, cmds)) {
					inform(a->c, _("The root password has been changed."));
					done = 1;
				} else {
					inform(a->c, _("An error occurred when "
					    "setting the root password."));
					done = 0;
				}
				commands_free(cmds);
			} else {
				/*
				 * Passwords don't match - tell the user, let them try again.
				 */
				inform(a->c, _("The passwords do not match."));
				done = 0;
			}
		} else {
			/*
			 * Cancelled by user
			 */
			done = 1;
		}

		dfui_response_free(r);
	}

	dfui_form_free(f);
}

void
fn_get_passphrase(struct i_fn_args *a)
{
	struct dfui_dataset *ds, *new_ds;
	struct dfui_form *f;
	struct dfui_response *r;
	const char *passphrase_1, *passphrase_2;
	int fd;
	int done = 0;

	f = dfui_form_create(
	    "crypt_passphrase",
	    _("Set Passphrase For Encryption"),
	    _("Please specify the passphrase to be used for the encrypted "
	    "filesystems.\n\n"
	    "Please note that in the LiveCD environment the keymap is set to "
	    "\"US ISO\". "
	    "If you prefer a different keymap for entering the passphrase "
	    "here, you will need to set it manually using kbdcontrol(1)."),
	    "",

	    "f", "passphrase_1", _("Passphrase"),
	    _("Enter the passphrase you would like to use for encryption"), "",
	    "p", "obscured", "true",
	    "f", "passphrase_2", _("Passphrase again"),
	    _("Enter the passphrase again to confirm"), "",
	    "p", "obscured", "true",

	    "a", "ok", _("Accept and Set Passphrase"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "passphrase_1", "");
	dfui_dataset_celldata_add(ds, "passphrase_2", "");
	dfui_form_dataset_add(f, ds);

	while (!done) {
		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
			new_ds = dfui_dataset_dup(dfui_response_dataset_get_first(r));
			dfui_form_datasets_free(f);
			dfui_form_dataset_add(f, new_ds);

			/*
			 * Fetch form field values.
			 */

			passphrase_1 = dfui_dataset_get_value(new_ds, "passphrase_1");
			passphrase_2 = dfui_dataset_get_value(new_ds, "passphrase_2");

			if (strlen(passphrase_1) == 0 && strlen(passphrase_2) == 0) {
				done = 0;
			} else if (strcmp(passphrase_1, passphrase_2) == 0) {
				/*
				 * Passphrases match, write it out.
				 */
				fd = open("/tmp/t1", O_RDWR | O_CREAT | O_TRUNC,
				    S_IRUSR);
				if (fd != -1) {
					write(fd, passphrase_1, strlen(passphrase_1));
					close(fd);
					done = 1;
				} else {
					inform(a->c, _("write() error"));
					done = 0;
				}
			} else {
				/*
				 * Passphrases don't match, tell the user,
				 * let them try again.
				 */
				inform(a->c, _("The passphrases do not match."));
				done = 0;
			}
		}

		dfui_response_free(r);
	}

	dfui_form_free(f);
}

void
fn_install_packages(struct i_fn_args *a)
{
	FILE *pfp;
	struct commands *cmds;
	struct dfui_celldata *cd;
	struct dfui_dataset *ds;
	struct dfui_field *fi;
	struct dfui_form *f;
	struct dfui_response *r;
	char command[256];
	char pkg_name[256];
	char msg_buf[1][1024];
	struct aura_dict *seen;

	snprintf(msg_buf[0], sizeof(msg_buf[0]),
	    _("Select optional software packages that you want "
	    "installed on this system.  This form lists only the "
	    "software packages installed on the LiveCD; thousands "
	    "more are available via the internet once %s "
	    "is installed."),
	    OPERATING_SYSTEM_NAME);

	f = dfui_form_create(
	    "install_packages",
	    _("Install Packages"),
	    msg_buf[0],
	    "",

	    "p", "special", "dfinstaller_install_packages",

	    NULL
	);

	ds = dfui_dataset_new();
	snprintf(command, 256, "ls %svar/db/pkg", a->os_root);
	if ((pfp = popen(command, "r")) != NULL) {
		while (fgets(pkg_name, 255, pfp) != NULL) {
			while (strlen(pkg_name) > 0 &&
			       isspace(pkg_name[strlen(pkg_name) - 1])) {
				pkg_name[strlen(pkg_name) - 1] = '\0';
			}
			fi = dfui_form_field_add(f, pkg_name,
			    dfui_info_new(pkg_name, "", ""));
			dfui_field_property_set(fi, "control", "checkbox");
			dfui_dataset_celldata_add(ds,
			    pkg_name, "Y");
		}
		pclose(pfp);
	}
	dfui_form_dataset_add(f, ds);

	dfui_form_action_add(f, "ok",
	    dfui_info_new(_("Accept and Install"), "", ""));
	dfui_form_action_add(f, "cancel",
	    dfui_info_new(_("Return to Configure Menu"), "", ""));

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		cmds = commands_new();
		seen = aura_dict_new(23, AURA_DICT_HASH);

		cd = dfui_dataset_celldata_get_first(dfui_response_dataset_get_first(r));

		while (cd != NULL) {
			strlcpy(pkg_name, dfui_celldata_get_field_id(cd), 256);
			if (!strcasecmp(dfui_celldata_get_value(cd), "Y")) {
				if (!pkg_copy(a, cmds, pkg_name, seen)) {
					inform(a->c, _("Couldn't install package `%s'."), pkg_name);
					break;
				}
			}
			cd = dfui_celldata_get_next(cd);
		}

		if (!commands_execute(a, cmds)) {
			inform(a->c, _("Packages were not fully installed."));
		} else {
			inform(a->c, _("Packages were successfully installed!"));
		}

		aura_dict_free(seen);
		commands_free(cmds);
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

void
fn_remove_packages(struct i_fn_args *a)
{
	FILE *pfp;
	struct commands *cmds;
	struct dfui_celldata *cd;
	struct dfui_dataset *ds;
	struct dfui_field *fi;
	struct dfui_form *f;
	struct dfui_response *r;
	char command[256];
	char pkg_name[256];
	struct aura_dict *seen;

	f = dfui_form_create(
	    "remove_packages",
	    _("Remove Packages"),
	    _("Select the installed software packages that you want "
	    "removed from this system."),
	    "",

	    "p", "special", "dfinstaller_remove_packages",

	    NULL
	);

	ds = dfui_dataset_new();
	snprintf(command, 256, "ls %smnt/var/db/pkg", a->os_root);
	if ((pfp = popen(command, "r")) != NULL) {
		while (fgets(pkg_name, 255, pfp)) {
			pkg_name[strlen(pkg_name) - 1] = '\0';
			fi = dfui_form_field_add(f, pkg_name,
			    dfui_info_new(pkg_name, "", ""));
			dfui_field_property_set(fi, "control", "checkbox");
			dfui_dataset_celldata_add(ds,
			    pkg_name, "N");
		}
		pclose(pfp);
	}
	dfui_form_dataset_add(f, ds);

	dfui_form_action_add(f, "ok",
	    dfui_info_new(_("Accept and Remove"), "", ""));
	dfui_form_action_add(f, "cancel",
	    dfui_info_new(_("Return to Configure Menu"), "", ""));

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		cmds = commands_new();
		seen = aura_dict_new(23, AURA_DICT_HASH);

		cd = dfui_dataset_celldata_get_first(dfui_response_dataset_get_first(r));

		while (cd != NULL) {
			strlcpy(pkg_name, dfui_celldata_get_field_id(cd), 256);
			if (!strcasecmp(dfui_celldata_get_value(cd), "Y")) {
				if (!pkg_remove(a, cmds, pkg_name, seen)) {
					inform(a->c, _("Couldn't remove package `%s'."), pkg_name);
					break;
				}
			}
			cd = dfui_celldata_get_next(cd);
		}

		if (!commands_execute(a, cmds)) {
			inform(a->c, _("Packages were not fully removed."));
		} else {
			inform(a->c, _("Packages were successfully removed."));
		}

		aura_dict_free(seen);
		commands_free(cmds);
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/** LIVECD UTILITIES FUNCTIONS **/

/*
 * String returned by this function must be deallocated by the caller.
 */
char *
fn_select_file(const char *title, const char *desc, const char *help, const char *cancel,
	       const char *dir, const char *ext, const struct i_fn_args *a)
{
	DIR *d;
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	struct dirent *de;
	char *s;
	struct aura_dict *dict;
	char *rk;
	size_t rk_len;

	f = dfui_form_create(
	    "select_file",
	    title, desc, help,
	    "p", "role", "menu",
	    NULL
	);

	dict = aura_dict_new(1, AURA_DICT_SORTED_LIST);
	d = opendir(dir);
	while ((de = readdir(d)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0 ||
		    strstr(de->d_name, ext) == NULL)
			continue;
		aura_dict_store(dict, de->d_name, strlen(de->d_name) + 1, "", 1);
	}
	closedir(d);

	aura_dict_rewind(dict);
	while (!aura_dict_eof(dict)) {
		aura_dict_get_current_key(dict, (void **)&rk, &rk_len),
		dfui_form_action_add(f, rk,
		    dfui_info_new(rk, "", ""));
		aura_dict_next(dict);
	}
	aura_dict_free(dict);

	k = dfui_form_action_add(f, "cancel",
	    dfui_info_new(cancel, "", ""));
	dfui_action_property_set(k, "accelerator", "ESC");

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	s = aura_strdup(dfui_response_get_action_id(r));

	dfui_form_free(f);
	dfui_response_free(r);

	return(s);
}

void
fn_set_kbdmap(struct i_fn_args *a)
{
	struct commands *cmds;
	char *s;
	char filename[256], keymapname[256];

	s = fn_select_file(_("Select Keyboard Map"),
	    _("Select a keyboard map appropriate to your keyboard layout."),
	    "", _("Return to Utilities Menu"), "/usr/share/syscons/keymaps",
	    ".kbd", a);

	if (strcmp(s, "cancel") != 0) {
		cmds = commands_new();
		command_add(cmds, "%s%s -l "
		    "/usr/share/syscons/keymaps/%s < /dev/ttyv0",
		    a->os_root, cmd_name(a, "KBDCONTROL"),
		    s);
		if (commands_execute(a, cmds)) {
			snprintf(filename, 256, "/usr/share/syscons/keymaps/%s", s);
			snprintf(keymapname, 256, "%s", filename_noext(basename(filename)));
			config_var_set(rc_conf, "keymap", keymapname);
		} else {
			inform(a->c, _("Keyboard map not successfully set."));
		}
		commands_free(cmds);
	}

	free(s);
}

void
fn_set_vidfont(struct i_fn_args *a)
{
	struct commands *cmds;
	char *s;
	char filename[256], variable[256], fontname[256];
	int by = 0;


	s = fn_select_file(_("Select Console Font"),
	    _("Select a font appropriate to your video monitor and language."),
	    "", _("Return to Utilities Menu"), "/usr/share/syscons/fonts",
	    ".fnt", a);

	if (strcmp(s, "cancel") != 0) {
		cmds = commands_new();
		command_add(cmds, "%s%s -f "
		    "/usr/share/syscons/fonts/%s < /dev/ttyv0",
		    a->os_root, cmd_name(a, "VIDCONTROL"),
		    s);
		if (commands_execute(a, cmds)) {
			if (strstr(s, "8x16") != NULL)
				by = 16;
			else if (strstr(s, "8x14") != NULL)
				by = 14;
			else
				by = 8;

			snprintf(variable, 256, "font8x%d", by);
			snprintf(filename, 256, "/usr/share/syscons/fonts/%s", s);
			snprintf(fontname, 256, "%s", filename_noext(basename(filename)));
			config_var_set(rc_conf, variable, fontname);

		} else {
			inform(a->c, _("Video font not successfully set."));
		}
		commands_free(cmds);
	}

	free(s);
}

void
fn_set_scrnmap(struct i_fn_args *a)
{
	struct commands *cmds;
	char *s;
	char filename[256], scrnmapname[256];

	s = fn_select_file(_("Select Screen Map"),
	    _("Select a mapping for translating characters as they appear "
	    "on your video console screen."),
	    "", _("Return to Utilities Menu"), "/usr/share/syscons/scrnmaps",
	    ".scm", a);

	if (strcmp(s, "cancel") != 0) {
		cmds = commands_new();
		command_add(cmds, "%s%s -l "
		    "/usr/share/syscons/scrnmaps/%s < /dev/ttyv0",
		    a->os_root, cmd_name(a, "VIDCONTROL"),
		    s);
		if (commands_execute(a, cmds)) {
			snprintf(filename, 256, "/usr/share/syscons/scrnmaps/%s", s);
			snprintf(scrnmapname, 256, "%s", filename_noext(basename(filename)));
			config_var_set(rc_conf, "scrnmap", scrnmapname);
		} else {
			inform(a->c, _("Video font not successfully set."));
		}
		commands_free(cmds);
	}
	free(s);
}

void
fn_set_timezone(struct i_fn_args *a)
{
	struct commands *cmds;
	char *s = NULL;
	char current_path[256], selection[256], temp[256];
	int found_file = 0;
	int result;

	result = dfui_be_present_dialog(a->c, _("Local or UTC (Greenwich Mean Time) clock"),
	    _("Yes|No"),
            _("Is this machine's CMOS clock set to UTC?\n\n"
	    "If it is set to local time, or you don't know, please choose NO here!"));
	if (result < 1)
		abort_backend();

	cmds = commands_new();
	switch (result) {
		case 1:
			command_add(cmds, "%s%s -f %s%setc/wall_cmos_clock",
			    a->os_root, cmd_name(a, "RM"),
			    a->os_root, a->cfg_root);
			break;
		case 2:
			command_add(cmds, "%s%s %s%setc/wall_cmos_clock",
			    a->os_root, cmd_name(a, "TOUCH"),
			    a->os_root, a->cfg_root);
			break;
	}
	commands_execute(a, cmds);

	snprintf(current_path, 256, "%s%susr/share/zoneinfo",
	    a->os_root, a->cfg_root);
	while (!found_file) {
		if (s != NULL)
			free(s);
		s = fn_select_file(_("Select Time Zone"),
		    _("Select a Time Zone appropriate to your physical location."),
		    "", _("Return to Utilities Menu"), current_path,
		    "", a);
		if (is_dir("%s/%s", current_path, s)) {
			snprintf(temp, 256, "%s/%s", current_path, s);
			strlcpy(current_path, temp, 256);
		} else {
			if (is_file("%s/%s", current_path, s)) {
				snprintf(selection, 256, "%s/%s", current_path, s);
				found_file = 1;
			}
			if (strcmp(s, "cancel") == 0) {
				strlcpy(selection, "cancel", 256);
				found_file = 1;
			}
		}
	}
	free(s);

	if (strcmp(selection, "cancel") != 0) {
		command_add(cmds, "%s%s %s %s%setc/localtime",
		    a->os_root, cmd_name(a, "CP"),
		    selection,
		    a->os_root, a->cfg_root);
		if (commands_execute(a, cmds))
			inform(a->c, _("The Time Zone has been set to %s."), selection);
	}
	commands_free(cmds);
}

void
fn_assign_datetime(struct i_fn_args *a)
{
	struct commands *cmds;
	struct dfui_dataset *ds, *new_ds;
	struct dfui_form *f;
	struct dfui_response *r;
	struct tm *tp;
	char temp[256];
	int year, month, dayofmonth, hour, minutes;
	int valid = 1;
	time_t now;

	now = time(NULL);
	tp = localtime(&now);

	f = dfui_form_create(
	    "set_datetime",
	    _("Set Time/Date"),
	    _("Enter the date-time in your timezone."),
	    "",

	    "f", "year", _("Enter year"),
	    _("Enter the current year (e.g. `2004')"), "",
	    "f", "month", _("Month"),
	    _("Enter the current month (e.g. `07')"), "",
	    "f", "dayofmonth", "dayofmonth",
	    _("Enter the current day of month (e.g. `30')"), "",
	    "f", "hour", "hour",
	    _("Enter the current hour (e.g. `07')"), "",
	    "f", "minutes", "minutes",
	    _("Enter the current minutes (e.g. `59')"), "",

	    "a", "ok", _("OK"), "", "",
	    "a", "cancel", _("Cancel"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	snprintf(temp, 256, "%i", (tp->tm_year+1900));
	dfui_dataset_celldata_add(ds, "year", temp);
	snprintf(temp, 256, "%i", (tp->tm_mon+1));
	dfui_dataset_celldata_add(ds, "month", temp);
	snprintf(temp, 256, "%i", tp->tm_mday);
	dfui_dataset_celldata_add(ds, "dayofmonth", temp);
	snprintf(temp, 256, "%i", tp->tm_hour);
	dfui_dataset_celldata_add(ds, "hour", temp);
	snprintf(temp, 256, "%i", tp->tm_min);
	dfui_dataset_celldata_add(ds, "minutes", temp);
	dfui_form_dataset_add(f, ds);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		new_ds = dfui_response_dataset_get_first(r);

		if ((year = atoi(dfui_dataset_get_value(new_ds, "year"))) <= 0)
			valid = 0;
		month = atoi(dfui_dataset_get_value(new_ds, "month"));
		if (month < 1 || month > 12)
			valid = 0;
		dayofmonth = atoi(dfui_dataset_get_value(new_ds, "dayofmonth"));
		if (dayofmonth < 1 || dayofmonth > 31)
			valid = 0;
		hour = atoi(dfui_dataset_get_value(new_ds, "hour"));
		if (hour < 0 || hour > 23)
			valid = 0;
		minutes = atoi(dfui_dataset_get_value(new_ds, "minutes"));
		if (minutes < 0 || minutes > 59)
			valid = 0;

		if (valid) {
			cmds = commands_new();
			command_add(cmds, "%s%s -n %04d%02d%02d%02d%02d",
			    a->os_root, cmd_name(a, "DATE"),
			    year, month, dayofmonth, hour, minutes);
			if (commands_execute(a, cmds)) {
				inform(a->c, _("The date and time have been set."));
			}
			commands_free(cmds);
		} else {
			inform(a->c, _("Please enter numbers within acceptable ranges "
				"for year, month, day of month, hour, and minute."));
		}
	}
}

void
fn_assign_hostname_domain(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *ds, *new_ds;
	struct config_vars *resolv_conf;
	const char *domain, *hostname;
	char *fqdn;

	f = dfui_form_create(
	    "set_hostname_domain",
	    _("Set Hostname/Domain"),
	    _("Please enter this machine's hostname and domain name."),
	    "",

	    "f", "hostname", _("Hostname"),
	    _("Enter the Hostname (e.g. `machine')"), "",
	    "f", "domain", _("Domain"),
	    _("Enter the Domain Name (e.g. `network.lan')"), "",

	    "a", "ok", _("OK"), "", "",
	    "a", "cancel", _("Cancel"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "hostname", "");
	dfui_dataset_celldata_add(ds, "domain", "");
	dfui_form_dataset_add(f, ds);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		new_ds = dfui_response_dataset_get_first(r);

		hostname = dfui_dataset_get_value(new_ds, "hostname");
		domain = dfui_dataset_get_value(new_ds, "domain");
		if (strlen(domain) == 0)
			asprintf(&fqdn, "%s", hostname);
		else
			asprintf(&fqdn, "%s.%s", hostname, domain);

		resolv_conf = config_vars_new();

		config_var_set(rc_conf, "hostname", fqdn);
		config_var_set(resolv_conf, "search", domain);
		config_vars_write(resolv_conf, CONFIG_TYPE_RESOLV,
		    "%s%setc/resolv.conf", a->os_root, a->cfg_root);

		config_vars_free(resolv_conf);

		free(fqdn);
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

void
fn_assign_ip(struct i_fn_args *a)
{
	FILE *p;
	struct commands *cmds;
	struct command *cmd;
	struct config_vars *resolv_conf;
	struct dfui_dataset *ds, *new_ds;
	struct dfui_form *f;
	struct dfui_action *k;
	struct dfui_response *r;
	const char *domain, *hostname;
	const char *interface_ip, *interface_netmask, *defaultrouter, *dns_resolver;
	char *string, *string1;
	char *word;
	char interface[256];
	char line[256];
	int write_config = 0;

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
	    _("Assign IP Address"),
	    _("Please select which interface you would like to configure:"),
	    "",
	    "p",	"role", "menu",
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

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		dfui_form_free(f);
		dfui_response_free(r);
		return;
	}

	strlcpy(interface, dfui_response_get_action_id(r), 256);

	resolv_conf = config_vars_new();

	switch (dfui_be_present_dialog(a->c, _("Use DHCP?"),
	    _("Use DHCP|Configure Manually"),
	    _("DHCP allows the interface to automatically obtain "
	    "an IP address from a nearby DHCP server.\n\n"
	    "Would you like to enable DHCP for %s?"), interface)) {
	case 1:
		asprintf(&string, "ifconfig_%s", interface);

		cmds = commands_new();
		cmd = command_add(cmds, "%s%s dhclient",
		    a->os_root, cmd_name(a, "KILLALL"));
		command_set_failure_mode(cmd, COMMAND_FAILURE_IGNORE);
		command_add(cmds, "%s%s %s",
		    a->os_root, cmd_name(a, "DHCLIENT"),
		    interface);
		if (commands_execute(a, cmds)) {
			/* XXX sleep(3); */
			show_ifconfig(a->c, interface);
			write_config = 1;
		} else {
			switch (dfui_be_present_dialog(a->c, _("DHCP Failure"),
			    _("Yes|No"),
			    _("Warning: could not enable dhclient for %s.\n\n"
			      "Write the corresponding settings to rc.conf "
			      "anyway?"), interface)) {
			case 1:
				write_config = 1;
				break;
			case 2:
				write_config = 0;
				break;
			default:
				abort_backend();
			}
		}
		commands_free(cmds);
		config_var_set(rc_conf, string, "DHCP");
		free(string);
		break;
	case 2:
		dfui_form_free(f);
		dfui_response_free(r);
		f = dfui_form_create(
		    "assign_ip",
		    _("Assign IP Address"),
		    _("Configuring Interface:"),
		    "",

		    "f", "interface_ip", _("IP Address"),
		    _("Enter the IP Address you would like to use"), "",
		    "f", "interface_netmask",	_("Netmask"),
		    _("Enter the netmask of the IP address"), "",
		    "f", "defaultrouter", _("Default Router"),
		    _("Enter the IP address of the default router"), "",
		    "f", "dns_resolver", _("Primary DNS Server"),
		    _("Enter the IP address of primary DNS Server"), "",
		    "f", "hostname", _("Hostname"),
		    _("Enter the Hostname"), "",
		    "f", "domain", _("Domain"),
		    _("Enter the Domain Name"), "",

		    "a", "ok", _("Configure Interface"),
		    "", "",
		    "a", "cancel", _("Return to Utilities Menu"),
		    "", "",
		    "p", "accelerator", "ESC",

		    NULL
		);

		ds = dfui_dataset_new();
		dfui_dataset_celldata_add(ds, "interface_netmask", "");
		dfui_dataset_celldata_add(ds, "defaultrouter", "");
		dfui_dataset_celldata_add(ds, "dns_resolver", "");
		dfui_dataset_celldata_add(ds, "hostname", "");
		dfui_dataset_celldata_add(ds, "domain", "");
		dfui_dataset_celldata_add(ds, "interface_ip", "");
		dfui_form_dataset_add(f, ds);

		if (!dfui_be_present(a->c, f, &r))
			abort_backend();

		if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
			new_ds = dfui_response_dataset_get_first(r);

			interface_ip = dfui_dataset_get_value(new_ds, "interface_ip");
			interface_netmask = dfui_dataset_get_value(new_ds, "interface_netmask");
			defaultrouter = dfui_dataset_get_value(new_ds, "defaultrouter");
			dns_resolver = dfui_dataset_get_value(new_ds, "dns_resolver");
			hostname = dfui_dataset_get_value(new_ds, "hostname");
			domain = dfui_dataset_get_value(new_ds, "domain");

			asprintf(&string, "ifconfig_%s", interface);
			asprintf(&string1, "inet %s netmask %s",
			    interface_ip, interface_netmask);

			cmds = commands_new();
			command_add(cmds, "%s%s %s %s netmask %s",
			    a->os_root, cmd_name(a, "IFCONFIG"),
			    interface, interface_ip, interface_netmask);
			command_add(cmds, "%s%s add default %s",
			    a->os_root, cmd_name(a, "ROUTE"),
			    defaultrouter);

			if (commands_execute(a, cmds)) {
				/* XXX sleep(3); */
				show_ifconfig(a->c, interface);
				write_config = 1;
			} else {
				switch (dfui_be_present_dialog(a->c,
				    _("ifconfig Failure"),
				    _("Yes|No"),
				    _("Warning: could not assign IP address "
				      "or default gateway.\n\n"
				      "Write the corresponding settings to "
				      "rc.conf anyway?"))) {
				case 1:
					write_config = 1;
					break;
				case 2:
					write_config = 0;
					break;
				default:
					abort_backend();
				}
			}
			commands_free(cmds);

			config_var_set(rc_conf, string, string1);
			config_var_set(rc_conf, "defaultrouter", defaultrouter);

			free(string);
			free(string1);

			asprintf(&string, "%s.%s", hostname, domain);
			config_var_set(rc_conf, "hostname", string);
			free(string);

			config_var_set(resolv_conf, "search", domain);
			config_var_set(resolv_conf, "nameserver", dns_resolver);
		}
		break;
	default:
		abort_backend();
	}

	if (write_config) {
		/*
		 * Save out changes to /etc/rc.conf and /etc/resolv.conf.
		 */
		config_vars_write(resolv_conf, CONFIG_TYPE_RESOLV,
		    "%s%setc/resolv.conf", a->os_root, a->cfg_root);
	}

	config_vars_free(resolv_conf);

	dfui_form_free(f);
	dfui_response_free(r);
}

static const char *
yes_to_y(const char *value)
{
	return(strcasecmp(value, "YES") == 0 ? "Y" : "N");
}

void
fn_select_services(struct i_fn_args *a)
{
	struct dfui_dataset *ds;
	struct dfui_form *f;
	struct dfui_response *r;

	if (!config_vars_read(a, rc_conf, CONFIG_TYPE_SH, "%s%setc/rc.conf",
		a->os_root, a->cfg_root)) {
		inform(a->c, _("Couldn't read %s%setc/rc.conf."),
		    a->os_root, a->cfg_root);
		a->result = 0;
		return;
	}

	f = dfui_form_create(
	    "select_services",
	    _("Select Services"),
	    _("Please select which services you would like started at boot time."),
	    "",

	    "f", "syslogd", "syslogd",
		_("System Logging Daemon"), "",
		"p", "control", "checkbox",
	    "f", "inetd", "inetd",
		_("Internet Super-Server"), "",
		"p", "control", "checkbox",
	    "f", "named", "named",
		_("BIND Name Server"), "",
		"p", "control", "checkbox",
	    "f", "ntpd", "ntpd",
		_("Network Time Protocol Daemon"), "",
		"p", "control", "checkbox",
	    "f", "sshd", "sshd",
		_("Secure Shell Daemon"), "",
		"p", "control", "checkbox",

	    "a", "ok", _("Enable/Disable Services"),
	        "", "",
	    "a", "cancel", _("Return to Utilities Menu"),
		"", "",
	        "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "syslogd",
	    yes_to_y(config_var_get(rc_conf, "syslogd_enable")));
	dfui_dataset_celldata_add(ds, "inetd",
	    yes_to_y(config_var_get(rc_conf, "inetd_enable")));
	dfui_dataset_celldata_add(ds, "named",
	    yes_to_y(config_var_get(rc_conf, "named_enable")));
	dfui_dataset_celldata_add(ds, "ntpd",
	    yes_to_y(config_var_get(rc_conf, "ntpd_enable")));
	dfui_dataset_celldata_add(ds, "sshd",
	    yes_to_y(config_var_get(rc_conf, "sshd_enable")));
	dfui_form_dataset_add(f, ds);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "cancel") == 0) {
		dfui_form_free(f);
		dfui_response_free(r);
		return;
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

/*** NON-fn_ FUNCTIONS ***/

/*
 * Caller is responsible for deallocation.
 */
static char *
convert_tmpfs_options(char *line)
{
	char *result, *word;
	int i;

	result = malloc(256);
	result[0] = '\0';

	for (; (word = strsep(&line, ",")) != NULL; ) {
		if (word[0] == '-') {
			/*
			 * Don't bother trying to honour the -C
			 * option, since we can't copy files from
			 * the right place anyway.
			 */
			if (strcmp(word, "-C") != 0) {
				for (i = 0; word[i] != '\0'; i++) {
					if (word[i] == '=')
						word[i] = ' ';
				}
				strlcat(result, word, 256);
				strlcat(result, " ", 256);
			}
		}
	}

	return(result);
}

/*
 * Uses ss->selected_{disk,slice} as the target system.
 */
int
mount_target_system(struct i_fn_args *a)
{
	FILE *crypttab, *fstab;
	struct commands *cmds;
	struct command *cmd;
	struct subpartition *a_subpart;
	char name[256], device[256], mtpt[256], fstype[256], options[256];
	char *filename, line[256];
	const char *try_mtpt[5]  = {"/var", "/tmp", "/usr", "/home", NULL};
	char *word, *cvtoptions;
	int i;

	/*
	 * Mount subpartitions from this installation if they are
	 * not already mounted.  Tricky, as we need to honour the
	 * installation's loader.conf and fstab.
	 */
	cmds = commands_new();

	/*
	 * First, unmount anything already mounted on /mnt.
	 */
	unmount_all_under(a, cmds, "%smnt", a->os_root);

	/*
	 * Reset and clear out subpartitions so that system
	 * can make a "dummy" subpart.
	 */
	subpartitions_free(storage_get_selected_slice(a->s));

	/*
	 * Create a temporary dummy subpartition - that we
	 * assume exists
	 */

	a_subpart = subpartition_new_ufs(storage_get_selected_slice(a->s),
	    "/dummy", 0, 0, 0, 0, 0, 0);

	/*
	 * Mount the target's / and read its /etc/fstab.
	 */
	if (use_hammer == 0) {
		command_add(cmds, "%s%s /dev/%s %s%s",
		    a->os_root, cmd_name(a, "MOUNT"),
		    subpartition_get_device_name(a_subpart),
		    a->os_root, a->cfg_root);
		cmd = command_add(cmds,
		    "%s%s -f %st2;"
		    "%s%s \"^[^#]\" %s%s/etc/crypttab >%st2",
		    a->os_root, cmd_name(a, "RM"), a->tmp,
		    a->os_root, cmd_name(a, "GREP"),
		    a->os_root, a->cfg_root, a->tmp);
		command_set_failure_mode(cmd, COMMAND_FAILURE_IGNORE);
	} else {
		command_add(cmds, "%s%s /dev/%s %sboot",
		    a->os_root, cmd_name(a, "MOUNT"),
		    subpartition_get_device_name(a_subpart),
		    a->os_root);
		cmd = command_add(cmds,
		    "%s%s -f %st2;"
		    "%s%s \"^vfs\\.root\\.realroot=\" %sboot/loader.conf >%st2",
		    a->os_root, cmd_name(a, "RM"), a->tmp,
		    a->os_root, cmd_name(a, "GREP"),
		    a->os_root, a->tmp);
		command_set_failure_mode(cmd, COMMAND_FAILURE_IGNORE);
	}
	if (!commands_execute(a, cmds)) {
		commands_free(cmds);
		return(0);
	}
	commands_free(cmds);
	cmds = commands_new();

	if (use_hammer) {
		struct stat sb = { .st_size = 0 };
		stat("/tmp/t2", &sb);
		if (sb.st_size > 0) {
			command_add(cmds, "%s%s %sboot",
			    a->os_root, cmd_name(a, "UMOUNT"),
			    a->os_root);
			fn_get_passphrase(a);
			command_add(cmds,
			    "%s%s -d /tmp/t1 luksOpen /dev/`%s%s \"^vfs\\.root\\.realroot=\" %st2 |"
			    "%s%s -Fhammer: '{print $2;}' |"
			    "%s%s -F: '{print $1;}'` root",
			    a->os_root, cmd_name(a, "CRYPTSETUP"),
			    a->os_root, cmd_name(a, "GREP"),
			    a->tmp,
			    a->os_root, cmd_name(a, "AWK"),
			    a->os_root, cmd_name(a, "AWK"));
			command_add(cmds,
			    "%s%s /dev/mapper/root %s%s",
			    a->os_root, cmd_name(a, "MOUNT_HAMMER"),
			    a->os_root, a->cfg_root);
		} else {
			command_add(cmds,
			    "%s%s /dev/`%s%s \"^vfs\\.root\\.mountfrom\" %sboot/loader.conf |"
			    "%s%s -Fhammer: '{print $2;}' |"
			    "%s%s 's/\"//'` %s%s",
			    a->os_root, cmd_name(a, "MOUNT_HAMMER"),
			    a->os_root, cmd_name(a, "GREP"),
			    a->os_root,
			    a->os_root, cmd_name(a, "AWK"),
			    a->os_root, cmd_name(a, "SED"),
			    a->os_root, a->cfg_root);
			command_add(cmds, "%s%s %sboot",
			    a->os_root, cmd_name(a, "UMOUNT"),
			    a->os_root);
		}
	}
	if (!commands_execute(a, cmds)) {
		commands_free(cmds);
		return(0);
	}
	commands_free(cmds);
	cmds = commands_new();

	/*
	 * Get rid of the dummy subpartition.
	 */
	subpartitions_free(storage_get_selected_slice(a->s));

	/*
	 * See if an /etc/crypttab exists.
	 */
	asprintf(&filename, "%s%s/etc/crypttab", a->os_root, a->cfg_root);
	crypttab = fopen(filename, "r");
	free(filename);
	if (crypttab != NULL) {
		if (!use_hammer)
			fn_get_passphrase(a);
		while (fgets(line, 256, crypttab) != NULL) {
			/*
			 * Parse the crypttab line.
			 */
			if (first_non_space_char_is(line, '#'))
				continue;
			if ((word = strtok(line, " \t")) == NULL)
				continue;
			strlcpy(name, word, 256);
			if (strcmp(name, "swap") == 0)
				continue;
			if ((word = strtok(NULL, " \t")) == NULL)
				continue;
			strlcpy(device, word, 256);

			command_add(cmds,
			    "%s%s -d /tmp/t1 luksOpen %s %s",
			    a->os_root, cmd_name(a, "CRYPTSETUP"),
			    device, name);

			continue;
		}
		fclose(crypttab);
	}
	if (!commands_execute(a, cmds)) {
		commands_free(cmds);
		return(0);
	}
	commands_free(cmds);

	asprintf(&filename, "%s%s/etc/fstab", a->os_root, a->cfg_root);
	fstab = fopen(filename, "r");
	free(filename);
	if (fstab == NULL) {
		inform(a->c, _("Filesystem table on installed system could not be read."));
		cmds = commands_new();
		command_add(cmds, "%s%s %s%s",
		    a->os_root, cmd_name(a, "UMOUNT"),
		    a->os_root, a->cfg_root);
		if (!commands_execute(a, cmds)) {
			inform(a->c, _("Warning: Installed system was not properly unmounted."));
		}
		commands_free(cmds);
		return(0);
	}

	cmds = commands_new();

	while (fgets(line, 256, fstab) != NULL) {
		/*
		 * Parse the fstab line.
		 */
		if (first_non_space_char_is(line, '#'))
			continue;
		if ((word = strtok(line, " \t")) == NULL)
			continue;
		strlcpy(device, word, 256);
		if ((word = strtok(NULL, " \t")) == NULL)
			continue;
		strlcpy(mtpt, word, 256);
		if ((word = strtok(NULL, " \t")) == NULL)
			continue;
		strlcpy(fstype, word, 256);
		if ((word = strtok(NULL, " \t")) == NULL)
			continue;
		strlcpy(options, word, 256);

		/*
		 * Now, if the mountpoint has /usr, /var, /tmp, or /home
		 * as a prefix, mount it under a->cfg_root.
		 */
		for (i = 0; try_mtpt[i] != NULL; i++) {
			if (strstr(mtpt, try_mtpt[i]) == mtpt) {
				/*
				 * Don't mount it if it's optional.
				 */
				if (strstr(options, "noauto") != NULL)
					continue;

				/*
				 * Don't mount it if device doesn't start
				 * with /dev/ or /pfs and it isn't 'tmpfs'.
				 */
				if (strstr(device, "/dev/") != NULL &&
				     strstr(device, "/pfs/") != NULL &&
				     strcmp(device, "tmpfs") != 0)
					continue;

				/*
				 * If the device is 'tmpfs', mount_tmpfs it instead.
				 */
				if (strcmp(device, "tmpfs") == 0) {
					cvtoptions = convert_tmpfs_options(options);
					command_add(cmds,
					    "%s%s %s tmpfs %s%s%s",
					    a->os_root, cmd_name(a, "MOUNT_TMPFS"),
					    cvtoptions, a->os_root, a->cfg_root, mtpt);
					free(cvtoptions);
				} else {
					if (use_hammer == 0) {
						command_add(cmds,
						    "%s%s -o %s %s%s %s%s%s",
						    a->os_root, cmd_name(a, "MOUNT"),
						    options,
						    a->os_root, device, a->os_root,
						    a->cfg_root, mtpt);
					} else {
						command_add(cmds,
						    "%s%s -o %s %s%s%s %s%s%s",
						    a->os_root, cmd_name(a, "MOUNT_NULL"),
						    options,
						    a->os_root, a->cfg_root, device,
						    a->os_root, a->cfg_root, mtpt);
					}
				}
			}
		}
	}
	fclose(fstab);

	if (!commands_execute(a, cmds)) {
		commands_free(cmds);
		return(0);
	}
	commands_free(cmds);

	return(1);
}
