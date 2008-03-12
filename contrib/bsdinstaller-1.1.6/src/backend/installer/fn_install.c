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
 * fn_install.c
 * Installer Function : Install OS Files.
 * $Id: fn_install.c,v 1.71 2005/02/07 06:46:20 cpressey Exp $
 */

#include <libgen.h>
#include <string.h>

#define SOURCES_CONF_FILE "/usr/local/share/dfuibe_installer/sources.conf"

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "aura/mem.h"
#include "aura/buffer.h"
#include "aura/fspred.h"

#include "dfui/dfui.h"
#include "dfui/system.h"

#include "installer/commands.h"
#include "installer/confed.h"
#include "installer/diskutil.h"
#include "installer/functions.h"
#include "installer/uiutil.h"

#include "flow.h"
#include "pathnames.h"
#include "fn.h"

/*
 * fn_install_os: actually put DragonFly on a disk.
 */
void
fn_install_os(struct i_fn_args *a)
{
	struct subpartition *sp;
	struct commands *cmds;
	struct command *cmd;
	int i, seen_it, prefix;
	FILE *sources_conf;
	char line[256];
	char cp_src[64][256];
	char file_path[256];
	int lines = 0;
	long mfsbacked_size;

	/* 
	 * If SOURCES_CONF_FILE exists, lets read in and 
	 * populate our copy sources.   If it does not exist
	 * simply set the list by hand.
	 */
        if (!is_file("%s%s", a->os_root, SOURCES_CONF_FILE)) {
		i_log(a, "Using internal copy sources table.");
		strcpy(cp_src[0],"/COPYRIGHT");
		strcpy(cp_src[1],"/var");
		strcpy(cp_src[2],"/bin");
		strcpy(cp_src[3],"/boot");
		strcpy(cp_src[4],"/cdrom");
		strcpy(cp_src[5],"/dev");
		strcpy(cp_src[6],"/etc");
		strcpy(cp_src[7],"/libexec");
		strcpy(cp_src[8],"/lib");
		strcpy(cp_src[9],"/kernel");
		strcpy(cp_src[10],"/modules");
		strcpy(cp_src[11],"/root");
		strcpy(cp_src[12],"/sbin");
		strcpy(cp_src[13],"/sys");
		strcpy(cp_src[14],"/tmp");
		strcpy(cp_src[15],"/usr/bin");
		strcpy(cp_src[16],"/usr/games");
		strcpy(cp_src[17],"/usr/include");
		strcpy(cp_src[18],"/usr/lib");
		strcpy(cp_src[19],"/usr/local");
		strcpy(cp_src[20],"/usr/local/OpenOffice.org1.1.3");
		strcpy(cp_src[21],"/usr/X11R6");
		strcpy(cp_src[22],"/usr/libdata");
		strcpy(cp_src[23],"/usr/libexec");
		strcpy(cp_src[24],"/usr/obj");
		strcpy(cp_src[25],"/usr/sbin");
		strcpy(cp_src[26],"/usr/share");
		strcpy(cp_src[27],"/usr/src");
		strcpy(cp_src[28],"");
	} else {
		snprintf(file_path, 256, "%s%s", a->os_root, SOURCES_CONF_FILE);
                sources_conf = fopen(file_path, "r");
		i_log(a, "Reading %s%s", a->os_root, SOURCES_CONF_FILE);
                while(fgets(line, 256, sources_conf) != NULL && lines < 63) {
			if(strlen(line)>0)
				line[strlen(line)-1] = '\0';
			strlcpy(cp_src[lines], line, 256);
			i_log(a,"Adding %s to copy source table.", cp_src[lines]);
			lines++;
                }
		i_log(a,"Added %i total items to copy source table.", lines);
		strcpy(cp_src[lines], "");
		fclose(sources_conf);
	}

	cmds = commands_new();

	/*
	 * If swap isn't mounted yet, mount it.
	 */
	if (measure_activated_swap(a) == 0) {
		for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
		    sp != NULL; sp = subpartition_next(sp)) {
			if (!subpartition_is_swap(sp))
				continue;
			command_add(cmds, "%s%s %sdev/%s",
			    a->os_root,
			    cmd_name(a, "SWAPON"),
			    a->os_root,
			    subpartition_get_device_name(sp));
		}
	}

	/*
	 * Unmount anything already mounted on /mnt.
	 */
	unmount_all_under(a, cmds, "%smnt", a->os_root);

	/*
	 * Create mount points and mount subpartitions on them.
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_swap(sp)) {
			/*
			 * Set this subpartition as the dump device.
			 */
#ifdef AUTOMATICALLY_ENABLE_CRASH_DUMPS
			if (subpartition_get_capacity(sp) < storage_get_memsize(a->s))
				continue;

			command_add(cmds, "%s%s -v %sdev/%s",
			    a->os_root, cmd_name(a, "DUMPON"),
			    a->os_root,
			    subpartition_get_device_name(sp));

			asprintf(&string, "/dev/%s",
			    subpartition_get_device_name(sp));
			config_var_set(rc_conf, "dumpdev", string);
			free(string);
			config_var_set(rc_conf, "dumpdir", "/var/crash");
#endif
			continue;
		}
	
		if (strcmp(subpartition_get_mountpoint(sp), "/") != 0) {
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, subpartition_get_mountpoint(sp));
		}

		/*
		 * Don't mount it if it's MFS-backed.
		 */

		if (subpartition_is_mfsbacked(sp)) {
			continue;
		}

		command_add(cmds, "%s%s %sdev/%s %smnt%s",
		    a->os_root, cmd_name(a, "MOUNT"),
		    a->os_root,
		    subpartition_get_device_name(sp),
		    a->os_root,
		    subpartition_get_mountpoint(sp));
	}

	/*
	 * Actually copy files now.
	 */

	for (i = 0; cp_src[i] != NULL && cp_src[i][0] != '\0'; i++) {
		char *src, *dest, *dn;

		dest = cp_src[i];

		/*
		 * If dest would be on an MFS-backed
		 * mountpoint, don't bother copying it.
		 */
		sp = subpartition_of(storage_get_selected_slice(a->s),
				     "%s%s", a->os_root, &dest[1]);
		if (sp != NULL && subpartition_is_mfsbacked(sp)) {
			continue;
		}

		/*
		 * Create intermediate directories, if needed.
		 */
		dn = dirname(dest);
		if (is_dir("%s%s", a->os_root, &dn[1]) &&
		    !is_dir("%smnt%s", a->os_root, dn)) {
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, dn);
		}
		aura_free(dn, "directory name");

		/*
		 * If a directory by the same name but with the suffix
		 * ".hdd" exists on the installation media, cpdup that
		 * instead.  This is particularly useful with /etc, which
		 * may have significantly different behaviour on the
		 * live CD compared to a standard HDD boot.
		 */
		if (is_dir("%s%s.hdd", a->os_root, &dest[1]))
			asprintf(&src, "%s.hdd", &dest[1]);
		else
			asprintf(&src, "%s", &dest[1]);

		if (is_dir("%s%s", a->os_root, src) || is_file("%s%s", a->os_root, src)) {
			/*
			 * Cpdup the chosen file or directory onto the HDD.
			 * if it exists on the source.
			 */
			cmd = command_add(cmds, "%s%s %s%s %smnt%s",
			    a->os_root, cmd_name(a, "CPDUP"),
			    a->os_root, src,
			    a->os_root, dest);
			command_set_log_mode(cmd, COMMAND_LOG_QUIET);
			aura_free(src, "source directory name");
		}
	}

	/*
	 * Now, because cpdup does not cross mount points,
	 * we must copy anything that the user might've made a
	 * seperate mount point for (e.g. /usr/libdata/lint.)
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		/*
		 * If the subpartition is a swap subpartition or an
		 * MFS-backed mountpoint, don't try to copy anything
		 * into it.
		 */
		if (subpartition_is_swap(sp) || subpartition_is_mfsbacked(sp))
			continue;

		/*
		 * If the mountpoint doesn't even exist on the installation
		 * medium, don't try to copy anything from it!  We assume
		 * it's an empty subpartition for the user's needs.
		 */
		if (!is_dir("%s%s", a->os_root, &subpartition_get_mountpoint(sp)[1]))
			continue;

		/*
		 * Don't bother copying the mountpoint IF:
		 * - we've already said to copy it, or something besides it
		 *   (it's a prefix of something in cp_src); or
		 * - we haven't said to copy it
		 *   (nothing in cp_src is a prefix of it.)
		 */
		seen_it = 0;
		prefix = 0;
		for (i = 0; cp_src[i] != NULL && cp_src[i][0] != '\0'; i++) {
			if (strncmp(subpartition_get_mountpoint(sp), cp_src[i],
			    strlen(subpartition_get_mountpoint(sp))) == 0) {
				seen_it = 1;
				break;
			}
			if (strncmp(cp_src[i], subpartition_get_mountpoint(sp),
			    strlen(cp_src[i])) == 0) {
				prefix = 1;
			}
		}
		if (seen_it || !prefix)
			continue;

		/*
		 * Otherwise, cpdup the subpartition.
		 *
		 * XXX check for .hdd-extended source dirs here, too,
		 * eventually - but for now, /etc.hdd will never be
		 * the kind of tricky sub-mount-within-a-mount-point
		 * that this part of the code is meant to handle.
		 */
		cmd = command_add(cmds, "%s%s %s%s %smnt%s",
		    a->os_root, cmd_name(a, "CPDUP"),
		    a->os_root, &subpartition_get_mountpoint(sp)[1],
		    a->os_root, subpartition_get_mountpoint(sp));
		command_set_log_mode(cmd, COMMAND_LOG_QUIET);
	}
	
	/*
	 * Create symlinks.
	 */

	/*
	 * If the user has both /var and /tmp subparitions,
	 * symlink /var/tmp to /tmp.
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "/tmp") != NULL &&
	    subpartition_find(storage_get_selected_slice(a->s), "/var") != NULL) {
		command_add(cmds, "%s%s 1777 %smnt/tmp",
		    a->os_root, cmd_name(a, "CHMOD"), a->os_root);
		command_add(cmds, "%s%s -rf %smnt/var/tmp",
		    a->os_root, cmd_name(a, "RM"), a->os_root);
		command_add(cmds, "%s%s -s /tmp %smnt/var/tmp",
		    a->os_root, cmd_name(a, "LN"), a->os_root);
	}

	/*
	 * If the user has /var, but no /tmp,
	 * symlink /tmp to /var/tmp.
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "/tmp") == NULL &&
	    subpartition_find(storage_get_selected_slice(a->s), "/var") != NULL) {
		command_add(cmds, "%s%s -rf %smnt/tmp",
		    a->os_root, cmd_name(a, "RM"), a->os_root);
		command_add(cmds, "%s%s -s /var/tmp %smnt/tmp",
		    a->os_root, cmd_name(a, "LN"), a->os_root);
	}

	/*
	 * If the user has /usr, but no /home,
	 * symlink /home to /usr/home.
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "/home") == NULL &&
	    subpartition_find(storage_get_selected_slice(a->s), "/usr") != NULL) {
		command_add(cmds, "%s%s -rf %smnt/home",
		    a->os_root, cmd_name(a, "RM"), a->os_root);
		command_add(cmds, "%s%s %smnt/usr/home",
		    a->os_root, cmd_name(a, "MKDIR"), a->os_root);
		command_add(cmds, "%s%s -s /usr/home %smnt/home",
		    a->os_root, cmd_name(a, "LN"), a->os_root);
	}

	/*
	 * XXX check for other possible combinations too?
	 */

	/*
	 * Clean up.  In case some file didn't make it, use rm -f
	 */
#ifdef __DragonFly__
	command_add(cmds, "%s%s -f %smnt/boot/loader.conf",
	    a->os_root, cmd_name(a, "RM"), a->os_root);
#endif
	command_add(cmds, "%s%s -f %smnt/tmp/install.log",
	    a->os_root, cmd_name(a, "RM"), a->os_root);

	/*
	 * Copy pristine versions over any files we might have installed.
	 * This allows the resulting file tree to be customized.
	 */
	for (i = 0; cp_src[i] != NULL && cp_src[i][0] != '\0'; i++) {
		char *src, *dest, *dn;

		src = cp_src[i];
		dest = cp_src[i];

		/*
		 * Get the directory that the desired thing to
		 * copy resides in.
		 */
		dn = dirname(dest);

		/*
		 * If this dir doesn't exist in PRISTINE_DIR
		 * on the install media, just skip it.
		 */
		if (!is_dir("%s%s%s", a->os_root, PRISTINE_DIR, dn)) {
			aura_free(dn, _("directory name"));
			continue;
		}

		/*
		 * Create intermediate directories, if needed.
		 */
		if (!is_dir("%smnt%s", a->os_root, dn)) {
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, dn);
		}
		aura_free(dn, "directory name");

		/*
		 * Cpdup the chosen file or directory onto the HDD.
		 */
		cmd = command_add(cmds, "%s%s %s%s %smnt%s",
		    a->os_root, cmd_name(a, "CPDUP"),
		    a->os_root, src,
		    a->os_root, dest);

		cmd = command_add(cmds,
		    "%s%s %s%s%s %smnt%s",
		    a->os_root, cmd_name(a, "CPDUP"),
		    a->os_root, PRISTINE_DIR, src,
		    a->os_root, dest);
		command_set_log_mode(cmd, COMMAND_LOG_QUIET);
	}
	
	/*
	 * Rebuild the user database, to get rid of any extra users
	 * from the LiveCD that aren't supposed to be installed
	 * (copying a pristine master.passwd isn't enough.)
	 */
	command_add(cmds, "%s%s -p -d %smnt/etc %smnt/etc/master.passwd",
	    a->os_root, cmd_name(a, "PWD_MKDB"), a->os_root, a->os_root);

	/* Create missing directories. */
	command_add(cmds, "%s%s %smnt/proc",
	    a->os_root, cmd_name(a, "MKDIR"), a->os_root);
	command_add(cmds, "%s%s %smnt/mnt",
	    a->os_root, cmd_name(a, "MKDIR"), a->os_root);

	/* Write new fstab. */

	command_add(cmds, "%s%s '%s' >%smnt/etc/fstab",
	    a->os_root, cmd_name(a, "ECHO"),
	    "# Device\t\tMountpoint\tFStype\tOptions\t\tDump\tPass#",
	    a->os_root);

	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (strcmp(subpartition_get_mountpoint(sp), "/") == 0) {
			command_add(cmds, "%s%s '/dev/%s\t\t%s\t\tufs\trw\t\t1\t1' >>%smnt/etc/fstab",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_get_device_name(sp),
			    subpartition_get_mountpoint(sp),
			    a->os_root);
		} else if (strcmp(subpartition_get_mountpoint(sp), "swap") == 0) {
			command_add(cmds, "%s%s '/dev/%s\t\tnone\t\tswap\tsw\t\t0\t0' >>%smnt/etc/fstab",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_get_device_name(sp),
			    a->os_root);
		} else if (subpartition_is_mfsbacked(sp)) {
                        mfsbacked_size = slice_get_capacity(storage_get_selected_slice(a->s)) * 2048;
                        command_add(cmds, "%s%s 'swap\t\t%s\t\t\tmfs\trw,-s%ld\t\t1\t1' >>%smnt/etc/fstab",
                                a->os_root, cmd_name(a, "ECHO"),
                                subpartition_get_mountpoint(sp),
                                mfsbacked_size,
                                a->os_root);
		} else {
			command_add(cmds, "%s%s '/dev/%s\t\t%s\t\tufs\trw\t\t2\t2' >>%smnt/etc/fstab",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_get_device_name(sp),
			    subpartition_get_mountpoint(sp),
			    a->os_root);
		}
	}

	command_add(cmds, "%s%s '%s' >>%smnt/etc/fstab",
	    a->os_root, cmd_name(a, "ECHO"),
	    "proc\t\t\t/proc\t\tprocfs\trw\t\t0\t0",
	    a->os_root);

	/* Backup the disklabel and the log. */

	command_add(cmds, "%s%s %s > %smnt/etc/disklabel.%s",
	    a->os_root, cmd_name(a, "DISKLABEL"),
	    slice_get_device_name(storage_get_selected_slice(a->s)),
	    a->os_root,
	    slice_get_device_name(storage_get_selected_slice(a->s)));
	command_add(cmds, "%s%s %sinstall.log %smnt/var/log/install.log",
	    a->os_root, cmd_name(a, "CP"),
	    a->tmp, a->os_root);
	command_add(cmds, "%s%s 600 %smnt/var/log/install.log",
	    a->os_root, cmd_name(a, "CHMOD"), a->os_root);

	/* Customize stuff here */
	if(is_file("%susr/local/bin/after_installation_routines.sh")) {
		command_add(cmds, "%susr/local/bin/after_installation_routines.sh",
		a->os_root, _("Running after installation custom routines..."));
	}

	/*
	 * Do it!
	 */
	/* commands_preview(cmds); */
	if (!commands_execute(a, cmds)) {
		inform(a->c, _("%s was not fully installed."), OPERATING_SYSTEM_NAME);
		a->result = 0;
	} else {
		a->result = 1;
	}
	commands_free(cmds);

	/*
	 * Unmount everything we mounted on /mnt.  This is done in a seperate
	 * command chain, so that partitions are unmounted, even if an error
	 * occurs in one of the preceding commands, or it is cancelled.
	 */
	cmds = commands_new();
	unmount_all_under(a, cmds, "%smnt", a->os_root);

	/*
	 * Once everything is unmounted, if the install went successfully,
	 * make sure once and for all that the disklabel is bootable.
	 */
	if (a->result) {
		command_add(cmds, "%s%s -B %s",
		    a->os_root, cmd_name(a, "DISKLABEL"),
		    slice_get_device_name(storage_get_selected_slice(a->s)));
	}

	if (!commands_execute(a, cmds))
		inform(a->c, _("Warning: subpartitions were not correctly unmounted."));

	commands_free(cmds);
}

