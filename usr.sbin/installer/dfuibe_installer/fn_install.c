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

/*
 * NOTE: Even though /var/run doesn't need to be backedup, nearly all
 *	 services depend on it so it is best to leave it on the root.
 */
static const char *nullfs_mountpt[] = {
	"/usr/obj", "/var/crash", "/var/cache",
	"/var/spool", "/var/log", "/var/tmp",
	NULL };
static const char *nullfs_mountname[] = {
	"/build/usr.obj", "/build/var.crash", "/build/var.cache",
	"/build/var.spool", "/build/var.log", "/build/var.tmp",
	NULL };

static void
handle_altfs(struct i_fn_args *a, struct commands *cmds)
{
	int i;

	/*
	 * Create directories for null mounts if not specified as a partition.
	 * (null mounts are from /build)
	 */
	for (i = 0; nullfs_mountpt[i]; ++i) {
		if (subpartition_find(storage_get_selected_slice(a->s), "%s", nullfs_mountpt[i]) != NULL)
			continue;

		/*
		 * Create directory infrastructure for null-mount if
		 * necessary, then issue the null mount(s).
		 */
		command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, nullfs_mountname[i]);
		command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root, nullfs_mountpt[i]);
		command_add(cmds, "%s%s %smnt%s %smnt%s",
		    a->os_root, cmd_name(a, "MOUNT_NULL"),
		    a->os_root, nullfs_mountname[i],
		    a->os_root, nullfs_mountpt[i]);
	}

	/*
	 * Create directory for /tmp and tmpfs mount if not specified as
	 * a partition.
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "%s", "/tmp") == NULL) {
		command_add(cmds, "%s%s -p %smnt/tmp",
			    a->os_root, cmd_name(a, "MKDIR"), a->os_root);
		command_add(cmds, "%s%s 1777 %smnt/tmp",
			    a->os_root, cmd_name(a, "CHMOD"), a->os_root);
		command_add(cmds, "%s%s dummy %smnt/tmp",
			    a->os_root, cmd_name(a, "MOUNT_TMPFS"), a->os_root);
	}
}

static void
unmount_altfs(struct i_fn_args *a __unused, struct commands *cmds __unused)
{
	return;
#if 0
	int i;

	/*
	 * Unmount null mounts
	 */
	i = sizeof(nullfs_mountpt) / sizeof(nullfs_mountpt[0]) - 1;
	while (i >= 0) {
		if (subpartition_find(storage_get_selected_slice(a->s), "%s", nullfs_mountpt[i]) != NULL)
			continue;

		/*
		 * Create directory infrastructure for null-mount if
		 * necessary, then issue the null mount(s).
		 */
		command_add(cmds, "%s%s %smnt%s",
			    a->os_root, cmd_name(a, "UMOUNT"),
			    a->os_root, nullfs_mountpt[i]);
		--i;
	}

	/*
	 * Unmount tmpfs mounts
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "%s", "/tmp") == NULL) {
		command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "UMOUNT"),
			    a->os_root, "/tmp");
	}
#endif
}

/*
 * fn_install_os: actually put DragonFly on a disk.
 */
void
fn_install_os(struct i_fn_args *a)
{
	struct subpartition *sp, *spnext;
	struct commands *cmds;
	struct command *cmd;
	int i, seen_it, prefix, j, needcrypt;
	FILE *sources_conf;
	char line[256];
	char cp_src[64][256];
	char file_path[256];
	char *string;
	int lines = 0;
	int nfsidx;

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
	unmount_altfs(a, cmds);
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
		if (strcmp(subpartition_get_mountpoint(sp), "/") == 0 ||
		    strcmp(subpartition_get_mountpoint(sp), "/build") == 0) {
			/* make sure mountpoint directory exists */
			command_add(cmds, "%s%s -p %smnt%s",
				    a->os_root, cmd_name(a, "MKDIR"),
				    a->os_root,
				    subpartition_get_mountpoint(sp));
			if (use_hammer == 1) {
				command_add(cmds, "%s%s /dev/%s %smnt%s",
				    a->os_root, cmd_name(a, "MOUNT_HAMMER"),
				    (subpartition_is_encrypted(sp) ?
				     fn_mapper_name(subpartition_get_device_name(sp), 0) : subpartition_get_device_name(sp)),
				    a->os_root,
				    subpartition_get_mountpoint(sp));
			} else {
				command_add(cmds, "%s%s /dev/%s %smnt%s",
				    a->os_root, cmd_name(a, "MOUNT"),
				    subpartition_is_encrypted(sp) ?
				     fn_mapper_name(subpartition_get_device_name(sp), 0) : subpartition_get_device_name(sp),
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

		/*
		 * mount everything except / and /build (which have already
		 * been mounted).  This should also get /boot.
		 */
		if (strcmp(subpartition_get_mountpoint(sp), "/") != 0 &&
		    strcmp(subpartition_get_mountpoint(sp), "/build") != 0) {
			/* make sure mountpoint directory exists */
			command_add(cmds, "%s%s -p %smnt%s",
			    a->os_root, cmd_name(a, "MKDIR"),
			    a->os_root,
			    subpartition_get_mountpoint(sp));
			/* Don't mount it if it's TMPFS-backed. */
			if (subpartition_is_tmpfsbacked(sp))
				continue;
			if (subpartition_is_encrypted(sp)) {
				command_add(cmds, "%s%s /dev/%s %smnt%s",
				    a->os_root, cmd_name(a, "MOUNT"),
				    fn_mapper_name(subpartition_get_device_name(sp), 0),
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
	 * Take care of tmpfs and null-mounts from /build
	 */
	handle_altfs(a, cmds);

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

		/*
		 * Cpdup the chosen file or directory onto the HDD.
		 * if it exists on the source.
		 */
		if (is_dir("%s%s", a->os_root, src) ||
		    is_file("%s%s", a->os_root, src)) {
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
	nfsidx = 0;
	sp = NULL;
	spnext = slice_subpartition_first(storage_get_selected_slice(a->s));

	for (;;) {
		const char *mountpt;

		/*
		 * Iterate nullfs mounts and then partitions
		 */
		if (nullfs_mountpt[nfsidx]) {
			mountpt = nullfs_mountpt[nfsidx];
			++nfsidx;
		} else {
			sp = spnext;
			if (sp == NULL)
				break;
			spnext = subpartition_next(sp);
			mountpt = subpartition_get_mountpoint(sp);
		}

		/*
		 * If the subpartition is a swap subpartition or an
		 * TMPFS-backed mountpoint, don't try to copy anything
		 * into it.
		 */
		if (sp) {
			if (subpartition_is_swap(sp) ||
			    subpartition_is_tmpfsbacked(sp)) {
				continue;
			}
		}

		/*
		 * If the mountpoint doesn't even exist on the installation
		 * medium, don't try to copy anything from it!  We assume
		 * it's an empty subpartition for the user's needs.
		 */
		if (!is_dir("%s%s", a->os_root, mountpt + 1))
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
			if (strncmp(mountpt, cp_src[i],
			    strlen(mountpt)) == 0) {
				seen_it = 1;
				break;
			}
			if (strncmp(cp_src[i], mountpt, strlen(cp_src[i])) == 0) {
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
			a->os_root, mountpt + 1,
			a->os_root, mountpt);
		command_set_log_mode(cmd, COMMAND_LOG_QUIET);
	}

	/*
	 * Create symlinks.
	 */

	/* Take care of /sys. */
	command_add(cmds, "%s%s -s usr/src/sys %smnt/sys",
	    a->os_root, cmd_name(a, "LN"), a->os_root);

	/*
	 * Make sure /home exists (goes on root mount otherwise).
	 */
	command_add(cmds, "%s%s -p %smnt/home",
		    a->os_root, cmd_name(a, "MKDIR"), a->os_root);

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

	/*
	 * Create missing directories for special mounts.
	 */
	command_add(cmds, "%s%s %smnt/proc",
	    a->os_root, cmd_name(a, "MKDIR"), a->os_root);
	command_add(cmds, "%s%s %smnt/dev",
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
		} else {
			const char *fsname;
			int order;

			/*
			 * fs type (/boot is always ufs)
			 */
			if (strcmp(subpartition_get_mountpoint(sp), "/boot") == 0)
				fsname = "ufs";
			else if (use_hammer)
				fsname = "hammer";
			else
				fsname = "ufs";

			if (strcmp(subpartition_get_mountpoint(sp), "/") == 0)
				order = 1;
			else
				order = 2;

			/*
			 * Adjust loader.conf for root partition
			 */
			if (strcmp(subpartition_get_mountpoint(sp), "/") == 0) {
				if (subpartition_is_encrypted(sp)) {
					command_add(cmds,
					    "%s%s 'vfs.root.mountfrom=\"ufs:md0s0\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    a->os_root);
					command_add(cmds,
					    "%s%s 'vfs.root.realroot=\"crypt:%s:%s:%s\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    fsname,
					    subpartition_get_device_name(sp),
					    fn_mapper_name(subpartition_get_device_name(sp), -1),
					    a->os_root);
				} else {
					command_add(cmds,
					    "%s%s 'vfs.root.mountfrom=\"%s:%s\"' >>%smnt/boot/loader.conf",
					    a->os_root, cmd_name(a, "ECHO"),
					    fsname,
					    subpartition_get_device_name(sp),
					    a->os_root);
				}
			}
			if (subpartition_is_tmpfsbacked(sp)) {
				command_add(cmds, "%s%s 'tmpfs\t\t\t%s\t\ttmpfs\trw,-s%luM\t1\t1' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_mountpoint(sp),
				    subpartition_get_capacity(sp),
				    a->os_root);
			} else if (subpartition_is_encrypted(sp)) {
				command_add(cmds, "%s%s '%s\t/dev/%s\tnone\tnone' >>%smnt/etc/crypttab",
				    a->os_root, cmd_name(a, "ECHO"),
				    fn_mapper_name(subpartition_get_device_name(sp), -1),
				    subpartition_get_device_name(sp),
				    a->os_root);
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\t%s\trw\t\t2\t2' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    fn_mapper_name(subpartition_get_device_name(sp), 0),
				    subpartition_get_mountpoint(sp),
				    fsname,
				    a->os_root);
			} else {
				command_add(cmds, "%s%s '/dev/%s\t\t%s\t\t%s\trw\t\t%d\t%d' >>%smnt/etc/fstab",
				    a->os_root, cmd_name(a, "ECHO"),
				    subpartition_get_device_name(sp),
				    subpartition_get_mountpoint(sp),
				    fsname,
				    order, order,
				    a->os_root);
			}
		}
	}

	/*
	 * Take care of NULL mounts from /build for things like /var/crash
	 * and /usr/obj if not specified as a discrete partition.
	 */
	for (j = 0; nullfs_mountpt[j] != NULL; j++) {
		if (subpartition_find(storage_get_selected_slice(a->s),
				      "%s", nullfs_mountpt[i]) != NULL) {
			continue;
		}
		command_add(cmds,
		    "%s%s '%s\t%s\t\tnull\trw\t\t0\t0' >>%smnt/etc/fstab",
		    a->os_root, cmd_name(a, "ECHO"),
		    nullfs_mountname[j],
		    nullfs_mountpt[j],
		    a->os_root);
	}

	/*
	 * Take care of /tmp as a tmpfs filesystem
	 */
	if (subpartition_find(storage_get_selected_slice(a->s), "/tmp") == NULL) {
		command_add(cmds,
		    "%s%s 'tmpfs\t/tmp\t\ttmpfs\trw\t\t0\t0' >>%smnt/etc/fstab",
		    a->os_root, cmd_name(a, "ECHO"), a->os_root);
	}

	/*
	 * Take care of /proc
	 */
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

#if 0
	/* 'chflags nohistory' as needed */
	for (j = 0; pfs_mountpt[j] != NULL; j++)
		if (pfs_nohistory[j] == 1)
			command_add(cmds, "%s%s -R nohistory %smnt%s",
			    a->os_root, cmd_name(a, "CHFLAGS"),
			    a->os_root, pfs_mountpt[j]);
#endif

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
		command_add(cmds,
		    "%s%s 'initrd.img_load=\"YES\"' >>%smnt/boot/loader.conf",
		    a->os_root, cmd_name(a, "ECHO"),
		    a->os_root);
		command_add(cmds,
		    "%s%s 'initrd.img_type=\"md_image\"' >>%smnt/boot/loader.conf",
		    a->os_root, cmd_name(a, "ECHO"),
		    a->os_root);
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
	unmount_altfs(a, cmds);
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

/*
 * /dev/mapper/
 *
 * (result is persistant until next call)
 */
const char *
fn_mapper_name(const char *mountpt, int withdev)
{
	const char *src;
	static char *save;

	src = strrchr(mountpt, '/');
	if (src == NULL || src[1] == 0)
		src = "root";
	else
		++src;

	if (save)
		free(save);
	switch(withdev) {
	case -1:
		asprintf(&save, "%s", src);
		break;
	case 0:
		asprintf(&save, "mapper/%s", src);
		break;
	case 1:
	default:
		asprintf(&save, "/dev/mapper/%s", src);
		break;
	}
	return save;
}
