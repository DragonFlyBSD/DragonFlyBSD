/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
#include "hammer2.h"

static void h2disk_check(const char *devpath,
		    void (*callback1)(const char *, hammer2_blockref_t *, int));
static void h2pfs_check(int fd, hammer2_blockref_t *bref,
		    void (*callback2)(const char *, hammer2_blockref_t *, int));

static void info_callback1(const char *, hammer2_blockref_t *, int);
static void info_callback2(const char *, hammer2_blockref_t *, int);

typedef void (*cmd_callback)(const char *, hammer2_blockref_t *, int);

static
void
h2disk_check_serno(cmd_callback fn)
{
	DIR *dir;

	if ((dir = opendir("/dev/serno")) != NULL) {
		struct dirent *den;

		while ((den = readdir(dir)) != NULL) {
			const char *ptr;
			int slice;
			char part;
			char *devpath;

			if (!strcmp(den->d_name, ".") ||
			    !strcmp(den->d_name, ".."))
				continue;
			ptr = strrchr(den->d_name, '.');
			if (ptr && sscanf(ptr, ".s%d%c", &slice, &part) == 2) {
				asprintf(&devpath, "/dev/serno/%s",
					 den->d_name);
				h2disk_check(devpath, fn);
				free(devpath);
			}
		}
		closedir(dir);
	}
}

static
void
h2disk_check_dm(cmd_callback fn)
{
	DIR *dir;

	if ((dir = opendir("/dev/mapper")) != NULL) {
		struct dirent *den;

		while ((den = readdir(dir)) != NULL) {
			char *devpath;

			if (!strcmp(den->d_name, ".") ||
			    !strcmp(den->d_name, "..") ||
			    !strcmp(den->d_name, "control"))
				continue;
			asprintf(&devpath, "/dev/mapper/%s",
				 den->d_name);
			h2disk_check(devpath, fn);
			free(devpath);
		}
		closedir(dir);
	}
}

static
void
h2disk_check_misc(cmd_callback fn)
{
	DIR *dir;

	if ((dir = opendir("/dev")) != NULL) {
		struct dirent *den;

		while ((den = readdir(dir)) != NULL) {
			char *devpath;

			if (!strcmp(den->d_name, ".") ||
			    !strcmp(den->d_name, ".."))
				continue;
			if (strncmp(den->d_name, "ad", 2) &&
			    strncmp(den->d_name, "vn", 2))
				continue;
			if (!strcmp(den->d_name, "vn"))
				continue;
			if (!strncmp(den->d_name, "ad", 2) &&
			    den->d_namlen <= 3)
				continue;
			asprintf(&devpath, "/dev/%s", den->d_name);
			h2disk_check(devpath, fn);
			free(devpath);
		}
		closedir(dir);
	}
}

int
cmd_info(int ac, const char **av)
{
	int i;

	for (i = 0; i < ac; ++i)
		h2disk_check(av[i], info_callback1);
	if (ac == 0) {
		h2disk_check_serno(info_callback1);
		h2disk_check_dm(info_callback1);
		h2disk_check_misc(info_callback1);
	}
	return 0;
}

static
void
info_callback1(const char *path, hammer2_blockref_t *bref, int fd)
{
	printf("%s:\n", path);
	h2pfs_check(fd, bref, info_callback2);
}

static
void
info_callback2(const char *pfsname,
	       hammer2_blockref_t *bref __unused, int fd __unused)
{
	printf("    %s\n", pfsname);
}

static void mount_callback1(const char *, hammer2_blockref_t *, int);
static void mount_callback2(const char *, hammer2_blockref_t *, int);
static void cmd_mountall_alarm(int signo);

static volatile sig_atomic_t DidAlarm;

int
cmd_mountall(int ac, const char **av)
{
	int i;
	pid_t pid;

	for (i = 0; i < ac; ++i)
		h2disk_check(av[i], mount_callback1);
	if (ac == 0) {
		h2disk_check_serno(mount_callback1);
		h2disk_check_dm(mount_callback1);
		h2disk_check_misc(mount_callback1);
	}
	signal(SIGALRM, cmd_mountall_alarm);
	for (;;) {
		alarm(15);
		pid = wait3(NULL, 0, NULL);
		if (pid < 0 && errno == ECHILD)
			break;
		if (pid < 0 && DidAlarm) {
			printf("Timeout waiting for mounts to complete\n");
			break;
		}
	}
	alarm(0);

	return 0;
}

static
void
cmd_mountall_alarm(int signo __unused)
{
	DidAlarm = 1;
}

static const char *mount_path;
static const char *mount_comp;

static
void
mount_callback1(const char *devpath, hammer2_blockref_t *bref, int fd)
{
	mount_path = devpath;
	mount_comp = strrchr(devpath, '/');
	if (mount_comp) {
		++mount_comp;
		h2pfs_check(fd, bref, mount_callback2);
	}
}

static
void
mount_callback2(const char *pfsname,
		hammer2_blockref_t *bref __unused, int fd)
{
	char *tmp_path;
	char *label;
	int tfd;

	if (strcmp(pfsname, "LOCAL") == 0) {
		if ((tfd = open("/dev/null", O_RDONLY)) >= 0) {
			dup2(tfd, fd);
			close(tfd);
		} else {
			perror("open(/dev/null)");
			exit(1);
		}
		asprintf(&tmp_path, "/var/hammer2/LOCAL.%s", mount_comp);
		asprintf(&label, "%s@LOCAL", mount_path);
		mkdir("/var/hammer2", 0700);
		mkdir(tmp_path, 0700);
		printf("mount %s\n", tmp_path);
		if (fork() == 0) {
			execl("/sbin/mount_hammer2",
			      "mount",
			      label,
			      tmp_path,
			      NULL);
		}
		free(label);
		free(tmp_path);
	}
}

static
void
h2disk_check(const char *devpath,
	     void (*callback1)(const char *, hammer2_blockref_t *, int))
{
	hammer2_blockref_t broot;
	hammer2_blockref_t best;
	hammer2_media_data_t media;
	struct partinfo partinfo;
	int fd;
	int i;
	int best_i;

	fd = open(devpath, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open \"%s\"\n", devpath);
		return;
	}
	if (ioctl(fd, DIOCGPART, &partinfo) == -1) {
		fprintf(stderr, "DIOCGPART failed on \"%s\"\n", devpath);
		goto done;
	}

	/*
	 * Check partition or slice for HAMMER2 designation.  Validate the
	 * designation either from the fstype (typically set for disklabel
	 * partitions), or the fstype_uuid (typically set for direct-mapped
	 * hammer2 GPT slices).
	 */
	if (partinfo.fstype != FS_HAMMER2) {
		uint32_t status;
		uuid_t h2uuid;
		int is_nil = uuid_is_nil(&partinfo.fstype_uuid, NULL);

		uuid_from_string(HAMMER2_UUID_STRING, &h2uuid, &status);
		if (!is_nil && (status != uuid_s_ok ||
		    uuid_compare(&partinfo.fstype_uuid, &h2uuid, NULL) != 0)) {
			goto done;
		}
	}

	/*
	 * Find the best volume header.
	 */
	best_i = -1;
	bzero(&best, sizeof(best));
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		bzero(&broot, sizeof(broot));
		broot.type = HAMMER2_BREF_TYPE_VOLUME;
		broot.data_off = (i * HAMMER2_ZONE_BYTES64) |
				 HAMMER2_PBUFRADIX;
		lseek(fd, broot.data_off & ~HAMMER2_OFF_MASK_RADIX, SEEK_SET);
		if (read(fd, &media, HAMMER2_PBUFSIZE) ==
		    (ssize_t)HAMMER2_PBUFSIZE &&
		    media.voldata.magic == HAMMER2_VOLUME_ID_HBO) {
			broot.mirror_tid = media.voldata.mirror_tid;
			if (best_i < 0 || best.mirror_tid < broot.mirror_tid) {
				best_i = i;
				best = broot;
			}
		}
	}
	if (best_i >= 0)
		callback1(devpath, &best, fd);
done:
	close(fd);
}

static
void
h2pfs_check(int fd, hammer2_blockref_t *bref,
	    void (*callback2)(const char *, hammer2_blockref_t *, int))
{
	hammer2_media_data_t media;
	hammer2_blockref_t *bscan;
	int bcount;
	int i;
	size_t bytes;
	size_t io_bytes;
	size_t boff;
	uint32_t cv;
	uint64_t cv64;
	hammer2_off_t io_off;
	hammer2_off_t io_base;

	bytes = (size_t)1 << (bref->data_off & HAMMER2_OFF_MASK_RADIX);

	io_off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	io_base = io_off & ~(hammer2_off_t)(HAMMER2_LBUFSIZE - 1);
	io_bytes = bytes;
	boff = io_off - io_base;

	io_bytes = HAMMER2_LBUFSIZE;
	while (io_bytes + boff < bytes)
		io_bytes <<= 1;

	if (io_bytes > sizeof(media)) {
		printf("(bad block size %zu)\n", bytes);
		return;
	}
	if (bref->type != HAMMER2_BREF_TYPE_DATA) {
		lseek(fd, io_base, SEEK_SET);
		if (read(fd, &media, io_bytes) != (ssize_t)io_bytes) {
			printf("(media read failed)\n");
			return;
		}
		if (boff)
			bcopy((char *)&media + boff, &media, bytes);
	}

	bscan = NULL;
	bcount = 0;

	/*
	 * Check data integrity in verbose mode, otherwise we are just doing
	 * a quick meta-data scan.  Meta-data integrity is always checked.
	 * (Also see the check above that ensures the media data is loaded,
	 * otherwise there's no data to check!).
	 */
	if (bref->type != HAMMER2_BREF_TYPE_DATA || VerboseOpt >= 1) {
		switch(HAMMER2_DEC_CHECK(bref->methods)) {
		case HAMMER2_CHECK_NONE:
			break;
		case HAMMER2_CHECK_DISABLED:
			break;
		case HAMMER2_CHECK_ISCSI32:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.iscsi32.value != cv) {
				printf("\t(icrc failed %02x:%08x/%08x)\n",
				       bref->methods,
				       bref->check.iscsi32.value,
				       cv);
			}
			break;
		case HAMMER2_CHECK_XXHASH64:
			cv64 = XXH64(&media, bytes, XXH_HAMMER2_SEED);
			if (bref->check.xxhash64.value != cv64) {
				printf("\t(xxhash failed %02x:%016jx/%016jx)\n",
				       bref->methods,
				       bref->check.xxhash64.value,
				       cv64);
			}
			break;
		case HAMMER2_CHECK_SHA192:
			break;
		case HAMMER2_CHECK_FREEMAP:
			cv = hammer2_icrc32(&media, bytes);
			if (bref->check.freemap.icrc32 != cv) {
				printf("\t(fcrc %02x:%08x/%08x)\n",
					bref->methods,
					bref->check.freemap.icrc32,
					cv);
			}
			break;
		}
	}

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		if (media.ipdata.meta.pfs_type == HAMMER2_PFSTYPE_SUPROOT) {
			if ((media.ipdata.meta.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
				bscan = &media.ipdata.u.blockset.blockref[0];
				bcount = HAMMER2_SET_COUNT;
			}
		} else if (media.ipdata.meta.op_flags & HAMMER2_OPFLAG_PFSROOT) {
			callback2((char*)media.ipdata.filename, bref, fd);
			bscan = NULL;
			bcount = 0;
		} else {
			bscan = NULL;
			bcount = 0;
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bscan = &media.npdata[0];
		bcount = bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bscan = &media.voldata.sroot_blockset.blockref[0];
		bcount = HAMMER2_SET_COUNT;
		break;
	default:
		break;
	}
	for (i = 0; i < bcount; ++i) {
		if (bscan[i].type != HAMMER2_BREF_TYPE_EMPTY)
			h2pfs_check(fd, &bscan[i], callback2);
	}
}
