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
 * $Id: fn_install.c,v 1.74 2006/04/18 19:43:48 joerg Exp $
 */

#include <libgen.h>
#include <string.h>

#define SOURCES_CONF_FILE "usr/share/installer/sources.conf"

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/buffer.h"
#include "libaura/fspred.h"

#include "libdfui/dfui.h"
#include "libdfui/system.h"

#include "libinstaller/commands.h"
#include "libinstaller/confed.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/uiutil.h"

#include "flow.h"
#include "pathnames.h"
#include "fn.h"

static const char *pfs_mountpt[8] = {"/var", "/tmp", "/usr", "/home",
	"/usr/obj", "/var/crash", "/var/tmp", NULL};

static const int pfs_nohistory[8] = {0, 1, 0, 0, 1, 1, 1};

static void
handle_pfs(struct i_fn_args *a, struct commands *cmds)
{
	int j;

	/*
	 * Create PFS root directory.
	 */
	command_add(cmds, "%s%s -p %smnt/pfs",
	    a->os_root, cmd_name(a, "MKDIR"),
	    a->os_root);

	for (j = 0; pfs_mountpt[j] != NULL; j++) {
		/*
		 * We have a PFS for a subdirectory, e.g. /var/crash, so we
		 * need to create /pfs/var.crash
		 */
		if (rindex(pfs_mountpt[j]+1, '/') != NULL) {
			command_add(cmds, "%s%s pfs-master %smnt/pfs%s.%s",
			    a->os_root, cmd_name(a, "HAMMER"),
			    a->os_root, dirname(pfs_mountpt[j]),
			    basename(pfs_mountpt[j]));
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, pfs_mountpt[j]);
			command_add(cmds, "%s%s %smnt/pfs%s.%s %smnt%s",
			    a->os_root, cmd_name(a, "MOUNT_NULL"),
			    a->os_root, dirname(pfs_mountpt[j]),
			    basename(pfs_mountpt[j]),
			    a->os_root, pfs_mountpt[j]);
		} else {
			command_add(cmds, "%s%s pfs-master %smnt/pfs%s",
			    a->os_root, cmd_name(a, "HAMMER"),
			    a->os_root, pfs_mountpt[j]);
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, pfs_mountpt[j]);
			command_add(cmds, "%s%s %smnt/pfs%s %smnt%s",
			    a->os_root, cmd_name(a, "MOUNT_NULL"),
			    a->os_root, pfs_mountpt[j],
			    a->os_root, pfs_mountpt[j]);
		}
	}
}

/*
 * fn_install_os: actually put DragonFly on a disk.
 */
void
fn_install_os(struct i_fn_args *a)
{
	struct subpartition *sp;
	struct commands *cmds;
	struct command *cmd;
	int i, seen_it, prefix, j, needcrypt;
	FILE *sources_conf;
	char line[256];
	char cp_src[64][256];
	char file_path[256];
	char *string;
	int lines = 0;

	/*
	 * Read SOURCES_CONF_FILE and populate our copy sources.
	 */
	snprintf(file_path, 256, "%s%s", a->os_root, SOURCES_CONF_FILE);
	sources_conf = fopen(file_path, "r");
	i_log(a, "Reading %s", file_path);
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

	cmds = commands_new();

	/*
	 * If swap isn't mounted yet, mount it.
	 */
	if (measure_activated_swap(a) == 0) {
		for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
		    sp != NULL; sp = subpartition_next(sp)) {
			if (!subpartition_is_swap(sp))
				continue;
			command_add(cmds, "%s%s /dev/%s",
			    a->os_root,
			    cmd_name(a, "SWAPON"),
			    subpartition_is_encrypted(sp) ?
			    "mapper/swap" : subpartition_get_device_name(sp));
		}
	}

	/*
	 * Unmount anything already mounted on /mnt.
	 */
	unmount_all_under(a, cmds, "%smnt", a->os_root);

	/* Check if crypto support is needed */
	needcrypt = 0;
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_encrypted(sp)) {
			needcrypt = 1;
			break;
		}
	}

	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (strcmp(subpartition_get_mountpoint(sp), "/") == 0) {
			if (use_hammer == 1) {
				command_add(cmds, "%s%s /dev/%s %smnt%s",
				    a->os_root, cmd_name(a, "MOUNT_HAMMER"),
				    subpartition_is_encrypted(sp) ?
				    "mapper/root" : subpartition_get_device_name(sp),
				    a->os_root,
				    subpartition_get_mountpoint(sp));
			} else {
				command_add(cmds, "%s%s /dev/%s %smnt%s",
				    a->os_root, cmd_name(a, "MOUNT"),
				    subpartition_get_device_name(sp),
				    a->os_root,
				    subpartition_get_mountpoint(sp));
			}
		}
	}

	/*
	 * Create mount points and mount subpartitions on them.
	 */
	for (sp = slice_subpartition_first(storage_get_selected_slice(a->s));
	     sp != NULL; sp = subpartition_next(sp)) {
		if (subpartition_is_swap(sp)) {
			/*
			 * Set this subpartition as the dump device.
			 */
			command_add(cmds, "%s%s -v /dev/%s",
			    a->os_root, cmd_name(a, "DUMPON"),
			    subpartition_is_encrypted(sp) ?
			    "mapper/swap" : subpartition_get_device_name(sp));

			asprintf(&string, "/dev/%s",
			    subpartition_is_encrypted(sp) ?
			    "mapper/swap" : subpartition_get_device_name(sp));
			config_var_set(rc_conf, "dumpdev", string);
			free(string);
			continue;
		}

		if (use_hammer == 0) {
			/* / is already mounted */
			if (strcmp(subpartition_get_mountpoint(sp), "/") != 0) {
				command_add(cmds, "%s%s -p %smnt%s",
				    a->os_root, cmd_name(a, "MKDIR"),
				    a->os_root,
				    subpartition_get_mountpoint(sp));
				/* Don't mount it if it's TMPFS-backed. */
				if (subpartition_is_tmpfsbacked(sp))
					continue;
				if (subpartition_is_encrypted(sp)) {
					command_add(cmds, "%s%s /dev/mapper/%s %smnt%s",
					    a->os_root, cmd_name(a, "MOUNT"),
					    subpartition_get_mountpoint(sp) + 1,
					    a->os_root,
					    subpartition_get_mountpoint(sp));
				} else {
					command_add(cmds, "%s%s /dev/%s %smnt%s",
					    a->os_root, cmd_name(a, "MOUNT"),
					    subpartition_get_device_name(sp),
					    a->os_root,
					    subpartition_get_mountpoint(sp));
				}
			}
		} else if (strcmp(subpartition_get_mountpoint(sp), "/boot") == 0) {
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root,
			    subpartition_get_mountpoint(sp));
			command_add(cmds, "%s%s /dev/%s %smnt%s",
			    a->os_root, cmd_name(a, "MOUNT"),
			    subpartition_get_device_name(sp),
			    a->os_root,
			    subpartition_get_mountpoint(sp));
		}
	}

	/*
	 * Take care of HAMMER PFS.
	 */
	if (use_hammer == 1)
		handle_pfs(a, cmds);

	/*
	 * Actually copy files now.
	 */

	for (i = 0; cp_src[i] != NULL && cp_src[i][0] != '\0'; i++) {
		char *src, *dest, *dn, *tmp_dest;

		dest = cp_src[i];

		/*
		 * If dest would be on an TMPFS-backed
		 * mountpoint, don't bother copying it.
		 */
		sp = subpartition_of(storage_get_selected_slice(a->s),
				     "%s%s", a->os_root, &dest[1]);
		if (sp != NULL && subpartition_is_tmpfsbacked(sp)) {
			continue;
		}

		/*
		 * Create intermediate directories, if needed.
		 */
		tmp_dest = aura_strdup(dest);
		dn = dirname(tmp_dest);
		if (is_dir("%s%s", a->os_root, &dn[1]) &&
		    !is_dir("%smnt%s", a->os_root, dn)) {
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, dn);
		}
		aura_free(tmp_dest, "directory name");

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
		 * TMPFS-backed mountpoint, don't try to copy anything
		 * into it.
		 */
		if (subpartition_is_swap(sp) || subpartition_is_tmpfsbacked(sp))
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

	/* Take care of /sys. */
	command_add(cmds, "%s%s -s usr/src/sys %smnt/sys",
	    a->os_root, cmd_name(a, "LN"), a->os_root);

	/*
	 * If the user has both /var and /tmp subpartitions,
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
	command_add(cmds, "%s%s -f %smnt/boot/loader.conf",
	    a->os_root, cmd_name(a, "RM"), a->os_root);
	command_add(cmds, "%s%s -f %smnt/tmp/install.log",
	    a->os_root, cmd_name(a, "RM"), a->os_root);
	command_add(cmds, "%s%s -f %smnt/tmp/t[12]",
	    a->os_root, cmd_name(a, "RM"), a->os_root);
	command_add(cmds, "%s%s -f %smnt/tmp/test_in",
	    a->os_root, cmd_name(a, "RM"), a->os_root);
	command_add(cmds, "%s%s -f %smnt/tmp/test_out",
	    a->os_root, cmd_name(a, "RM"), a->os_root);

	/*
	 * Copy pristine versions over any files we might have installed.
	 * This allows the resulting file tree to be customized.
	 */
	for (i = 0; cp_src[i] != NULL && cp_src[i][0] != '\0'; i++) {
		char *src, *dest, *dn, *tmp_dest;

		src = cp_src[i];
		dest = cp_src[i];

		/*
		 * Get the directory that the desired thing to
		 * copy resides in.
		 */
		tmp_dest = aura_strdup(dest);
		dn = dirname(tmp_dest);

		/*
		 * If this dir doesn't exist in PRISTINE_DIR
		 * on the install media, just skip it.
		 */
		if (!is_dir("%s%s%s", a->os_root, PRISTINE_DIR, dn)) {
			aura_free(tmp_dest, _("directory name"));
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
		aura_free(tmp_dest, "directory name");

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
		if (strcmp(subpartition_get_mountpoint(sp), "swap") == 0) {
			command_add(cmds, "%s%s '/dev/%s\t\tnone\t\tswap\tsw\t\t0\t0' >>%smnt/etc/fstab",
			    a->os_root, cmd_name(a, "ECHO"),
			    subpartition_is_encrypted(sp) ?
			    "mapper/swap" : subpartition_get_device_name(sp),
			    a->os_root);
			if (subpartition_is_encrypted(sp)) {
				command_add(cmds,
				    "%s%s 'swap\t/dev/%s\tnone\tnone' >>%smnt/etc/crypttab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    a->os_root);
			}
		} else if (use_hammer == 0) {
			if (strcmp(subpartition_get_mountpoint(sp), "/") == 0) {
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\tufs\trw\t\t1\t1' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    subpartition_get_mountpoint(sp),
				    a->os_root);
			} else if (subpartition_is_tmpfsbacked(sp)) {
				command_add(cmds, "%s%s 'tmpfs\t\t\t%s\t\ttmpfs\trw,-s%luM\t1\t1' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_mountpoint(sp),
				    subpartition_get_capacity(sp),
				    a->os_root);
			} else if (subpartition_is_encrypted(sp)) {
				command_add(cmds, "%s%s '%s\t/dev/%s\tnone\tnone' >>%smnt/etc/crypttab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_mountpoint(sp) + 1,
				    subpartition_get_device_name(sp),
				    a->os_root);
				command_add(cmds, "%s%s '/dev/mapper/%s\t\t%s\t\tufs\trw\t\t2\t2' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_mountpoint(sp) + 1,
				    subpartition_get_mountpoint(sp),
				    a->os_root);
			} else {
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\tufs\trw\t\t2\t2' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    subpartition_get_mountpoint(sp),
				    a->os_root);
			}
		} else {
			if (strcmp(subpartition_get_mountpoint(sp), "/") == 0) {
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\thammer\trw\t\t1\t1' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    subpartition_get_mountpoint(sp),
				    a->os_root);
				if (subpartition_is_encrypted(sp)) {
					command_add(cmds,
					    "%s%s 'vfs.root.mountfrom=\"ufs:md0s0\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    a->os_root);
					command_add(cmds,
					    "%s%s 'vfs.root.realroot=\"crypt:hammer:%s:root\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    subpartition_get_device_name(sp),
					    a->os_root);
				} else {
					command_add(cmds,
					    "%s%s 'vfs.root.mountfrom=\"hammer:%s\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    subpartition_get_device_name(sp),
					    a->os_root);
				}
			} else if (strcmp(subpartition_get_mountpoint(sp), "/boot") == 0) {
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\tufs\trw\t\t1\t1' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    subpartition_get_mountpoint(sp),
				    a->os_root);
			}
		}
	}

	/*
	 * Take care of HAMMER PFS null mounts.
	 */
	if (use_hammer == 1) {
		for (j = 0; pfs_mountpt[j] != NULL; j++) {
			if (rindex(pfs_mountpt[j]+1, '/') != NULL)
				command_add(cmds, "%s%s '/pfs%s.%s\t%s\t\tnull\trw\t\t0\t0' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    dirname(pfs_mountpt[j]),
				    basename(pfs_mountpt[j]),
				    pfs_mountpt[j],
				    a->os_root);
			else
				command_add(cmds, "%s%s '/pfs%s\t\t%s\t\tnull\trw\t\t0\t0' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    pfs_mountpt[j],
				    pfs_mountpt[j],
				    a->os_root);
		}
	}

	command_add(cmds, "%s%s '%s' >>%smnt/etc/fstab",
	    a->os_root, cmd_name(a, "ECHO"),
	    "proc\t\t\t/proc\t\tprocfs\trw\t\t0\t0",
	    a->os_root);

	/* Backup the disklabel and the log. */
	command_add(cmds, "%s%s %s > %smnt/etc/disklabel.%s",
	    a->os_root, cmd_name(a, "DISKLABEL64"),
	    slice_get_device_name(storage_get_selected_slice(a->s)),
	    a->os_root,
	    slice_get_device_name(storage_get_selected_slice(a->s)));

	/* 'chflags nohistory' as needed */
	for (j = 0; pfs_mountpt[j] != NULL; j++)
		if (pfs_nohistory[j] == 1)
			command_add(cmds, "%s%s -R nohistory %smnt%s",
			    a->os_root, cmd_name(a, "CHFLAGS"),
			    a->os_root, pfs_mountpt[j]);

	/* Create the rescue image. */
	command_add(cmds, "%s%s -b %smnt/boot -t %smnt/tmp",
	    a->os_root, cmd_name(a, "MKINITRD"),
	    a->os_root, a->os_root);
	command_add(cmds, "%s%s -rf %smnt/tmp/initrd",
	    a->os_root, cmd_name(a, "RM"), a->os_root);

	/* Do some preparation if encrypted partitions were configured */
	if (needcrypt) {
		command_add(cmds,
		    "%s%s 'dm_load=\"yes\"' >>%smnt/boot/loader.conf",
		    a->os_root, cmd_name(a, "ECHO"),
		    a->os_root);
		command_add(cmds,
		    "%s%s 'dm_target_crypt_load=\"yes\"' >>%smnt/boot/loader.conf",
		    a->os_root, cmd_name(a, "ECHO"),
		    a->os_root);
		if (use_hammer) {
			command_add(cmds,
			    "%s%s 'initrd.img_load=\"YES\"' >>%smnt/boot/loader.conf",
			    a->os_root, cmd_name(a, "ECHO"),
			    a->os_root);
			command_add(cmds,
			    "%s%s 'initrd.img_type=\"md_image\"' >>%smnt/boot/loader.conf",
			    a->os_root, cmd_name(a, "ECHO"),
			    a->os_root);
		}
	}

	/* Customize stuff here */
	if(is_file("%susr/local/bin/after_installation_routines.sh", a->os_root)) {
		command_add(cmds, "%susr/local/bin/after_installation_routines.sh",
		    a->os_root);
	}

	/* Save the installation log. */
	command_add(cmds, "%s%s %sinstall.log %smnt/var/log/install.log",
	    a->os_root, cmd_name(a, "CP"),
	    a->tmp, a->os_root);
	command_add(cmds, "%s%s 600 %smnt/var/log/install.log",
	    a->os_root, cmd_name(a, "CHMOD"), a->os_root);

	/*
	 * Do it!
	 */
	/* commands_preview(a->c, cmds); */
	if (!commands_execute(a, cmds)) {
		inform(a->c, _("%s was not fully installed."), OPERATING_SYSTEM_NAME);
		a->result = 0;
	} else {
		a->result = 1;
	}
	commands_free(cmds);
	cmds = commands_new();

	if (a->result) {
		config_vars_write(rc_conf, CONFIG_TYPE_SH, "%smnt/etc/rc.conf",
		    a->os_root);
		config_vars_free(rc_conf);
		rc_conf = config_vars_new();
	}

	/*
	 * Unmount everything we mounted on /mnt.  This is done in a seperate
	 * command chain, so that partitions are unmounted, even if an error
	 * occurs in one of the preceding commands, or it is cancelled.
	 */
	unmount_all_under(a, cmds, "%smnt", a->os_root);

	/*
	 * Once everything is unmounted, if the install went successfully,
	 * make sure once and for all that the disklabel is bootable.
	 */
	if (a->result)
		command_add(cmds, "%s%s -B %s",
		    a->os_root, cmd_name(a, "DISKLABEL64"),
		    slice_get_device_name(storage_get_selected_slice(a->s)));

	if (!commands_execute(a, cmds))
		inform(a->c, _("Warning: subpartitions were not correctly unmounted."));

	commands_free(cmds);

	/*
	 * Finally, remove all swap and any mappings.
	 */
	if (swapoff_all(a) == NULL)
		inform(a->c, _("Warning: swap could not be turned off."));
	if (remove_all_mappings(a) == NULL)
		inform(a->c, _("Warning: mappings could not be removed."));
}
