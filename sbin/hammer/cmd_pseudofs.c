/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sbin/hammer/cmd_pseudofs.c,v 1.3 2008/07/02 22:05:59 dillon Exp $
 */

#include "hammer.h"

static void parse_pfsd_options(char **av, int ac, hammer_pseudofs_data_t pfsd);
static void init_pfsd(hammer_pseudofs_data_t pfsd);
static void dump_pfsd(hammer_pseudofs_data_t pfsd);
static void pseudofs_usage(int code);

void
hammer_cmd_pseudofs_status(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;
	int i;
	int fd;

	for (i = 0; i < ac; ++i) {
		bzero(&pfsd, sizeof(pfsd));
		bzero(&pfs, sizeof(pfs));
		pfs.ondisk = &pfsd;
		pfs.bytes = sizeof(pfsd);
		printf("%s\t", av[i]);
		fd = open(av[i], O_RDONLY);
		if (fd < 0 || ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
			printf("Not a HAMMER root\n");
		} else {
			printf("Pseudo-fs #0x%08x {\n", pfs.pseudoid);
			dump_pfsd(&pfsd);
			printf("}\n");
		}
	}
}

void
hammer_cmd_pseudofs_create(char **av, int ac)
{
	if (ac == 0)
		pseudofs_usage(1);

	if (mknod(av[0], S_IFDIR|0777, 0) < 0) {
		perror("mknod (create pseudofs):");
		exit(1);
	}
	hammer_cmd_pseudofs_update(av, ac, 1);
}

void
hammer_cmd_pseudofs_update(char **av, int ac, int doinit)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	pfs.version = HAMMER_IOC_PSEUDOFS_VERSION;

	printf("%s\t", av[0]);
	fflush(stdout);
	fd = open(av[0], O_RDONLY);

	if (fd >= 0 && ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
		if (doinit) {
			printf("Pseudo-fs #0x%08x created\n", pfs.pseudoid);
			init_pfsd(&pfsd);
		} else {
			printf("\n");
		}
		parse_pfsd_options(av + 1, ac - 1, &pfsd);
		pfs.bytes = sizeof(pfsd);
		if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) == 0) {
			if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
				dump_pfsd(&pfsd);
			} else {
				printf("Unable to retrieve pfs configuration after successful update: %s\n", strerror(errno));
				exit(1);
			}
		} else {
			printf("Unable to adjust pfs configuration: %s\n", strerror(errno));
			exit(1);
		}
	} else {
		printf("PFS Creation failed: %s\n", strerror(errno));
		exit(1);
	}
}

static void
init_pfsd(hammer_pseudofs_data_t pfsd)
{
	uint32_t status;

	pfsd->sync_beg_tid = 1;
	pfsd->sync_end_tid = 1;
	pfsd->sync_beg_ts = 0;
	pfsd->sync_end_ts = 0;
	uuid_create(&pfsd->shared_uuid, &status);
	uuid_create(&pfsd->unique_uuid, &status);
	pfsd->master_id = 0;
}

static
void
dump_pfsd(hammer_pseudofs_data_t pfsd)
{
	u_int32_t status;
	char *str = NULL;

	printf("    sync-beg-tid=0x%016llx\n", pfsd->sync_beg_tid);
	printf("    sync-end-tid=0x%016llx\n", pfsd->sync_end_tid);
	uuid_to_string(&pfsd->shared_uuid, &str, &status);
	printf("    shared-uuid=%s\n", str);
	uuid_to_string(&pfsd->unique_uuid, &str, &status);
	printf("    unique-uuid=%s\n", str);
	if (pfsd->mirror_flags & HAMMER_PFSD_SLAVE) {
		printf("    slave\n");
	} else if (pfsd->master_id < 0) {
		printf("    no-mirror\n");
	} else {
		printf("    master=%d\n", pfsd->master_id);
	}
	printf("    label=\"%s\"\n", pfsd->label);
}

static void
parse_pfsd_options(char **av, int ac, hammer_pseudofs_data_t pfsd)
{
	char *cmd;
	char *ptr;
	int len;
	uint32_t status;

	while (ac) {
		cmd = *av;
		if ((ptr = strchr(cmd, '=')) != NULL)
			*ptr++ = 0;

		/*
		 * Basic assignment value test
		 */
		if (strcmp(cmd, "no-mirror") == 0 ||
		    strcmp(cmd, "slave") == 0) {
			if (ptr) {
				fprintf(stderr,
					"option %s should not have "
					"an assignment\n",
					cmd);
				exit(1);
			}
		} else {
			if (ptr == NULL) {
				fprintf(stderr,
					"option %s requires an assignment\n",
					cmd);
				exit(1);
			}
		}

		status = uuid_s_ok;
		if (strcmp(cmd, "sync-beg-tid") == 0) {
			pfsd->sync_beg_tid = strtoull(ptr, NULL, 16);
		} else if (strcmp(cmd, "sync-end-tid") == 0) {
			pfsd->sync_end_tid = strtoull(ptr, NULL, 16);
		} else if (strcmp(cmd, "shared-uuid") == 0) {
			uuid_from_string(ptr, &pfsd->shared_uuid, &status);
		} else if (strcmp(cmd, "unique-uuid") == 0) {
			uuid_from_string(ptr, &pfsd->unique_uuid, &status);
		} else if (strcmp(cmd, "master") == 0) {
			pfsd->master_id = strtol(ptr, NULL, 0);
			pfsd->mirror_flags &= ~HAMMER_PFSD_SLAVE;
		} else if (strcmp(cmd, "slave") == 0) {
			pfsd->master_id = -1;
			pfsd->mirror_flags |= HAMMER_PFSD_SLAVE;
		} else if (strcmp(cmd, "no-mirror") == 0) {
			pfsd->master_id = -1;
			pfsd->mirror_flags &= ~HAMMER_PFSD_SLAVE;
		} else if (strcmp(cmd, "label") == 0) {
			len = strlen(ptr);
			if (ptr[0] == '"' && ptr[len-1] == '"') {
				ptr[len-1] = 0;
				++ptr;
			} else if (ptr[0] == '"') {
				fprintf(stderr,
					"option %s: malformed string\n",
					cmd);
				exit(1);
			}
			snprintf(pfsd->label, sizeof(pfsd->label), "%s", ptr);
		} else {
			fprintf(stderr, "invalid option: %s\n", cmd);
			exit(1);
		}
		if (status != uuid_s_ok) {
			fprintf(stderr, "option %s: error parsing uuid %s\n",
				cmd, ptr);
			exit(1);
		}
		--ac;
		++av;
	}
}

static
void
pseudofs_usage(int code)
{
	fprintf(stderr, 
		"hammer pfs-status <dirpath1>...<dirpathN>\n"
		"hammer pfs-create <dirpath> [options]\n"
		"hammer pfs-update <dirpath> [options]\n"
		"\n"
		"    sync-beg-tid=0x16llx\n"
		"    sync-end-tid=0x16llx\n"
		"    shared-uuid=0x16llx\n"
		"    unique-uuid=0x16llx\n"
		"    master=0-15\n"
		"    slave\n"
		"    no-mirror\n"
		"    label=\"string\"\n"
	);
	exit(code);
}

