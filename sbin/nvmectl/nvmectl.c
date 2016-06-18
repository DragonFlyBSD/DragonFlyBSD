/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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
 */

#include "nvmectl.h"

typedef int (*cmd_t)(int ac, char **av, const char *id, int fd);

static cmd_t parsecmd(int ac, char **av, int *globokp);
static int cmd_info(int ac, char **av, const char *id, int fd);
static int cmd_errors(int ac, char **av, const char *id, int fd);
static void usage(int rc);

int VerboseOpt;

int
main(int ac, char **av)
{
	int rc = 0;
	int ch;
	int nvmei;
	int globok;
	int i;
	cmd_t cmd;

	while ((ch = getopt(ac, av, "v")) != -1) {
		switch(ch) {
		case 'v':
			++VerboseOpt;
			break;
		default:
			usage(1);
			break;
		}
	}

	ac -= optind;
	av += optind;

	for (nvmei = ac; nvmei > 0; --nvmei) {
		if (strncmp(av[nvmei - 1], "nvme", 4) != 0)
			break;
	}
	if (nvmei == 0)
		usage(1);

	globok = 0;
	cmd = parsecmd(nvmei, av, &globok);

	if (nvmei == ac && globok) {
		i = 0;
		for (;;) {
			char *path;
			int fd;

			if (i)
				printf("\n");
			asprintf(&path, "/dev/nvme%d", i);
			fd = open(path, O_RDWR);
			free(path);
			if (fd < 0)
				break;
			rc += cmd(nvmei, av, path + 5, fd);
			close(fd);
			++i;
		}
	} else if (nvmei == ac && !globok) {
		fprintf(stderr, "must specify nvmeX device for command\n");
	} else {
		for (i = nvmei; i < ac; ++i) {
			char *path;
			int fd;

			if (i != nvmei)
				printf("\n");

			asprintf(&path, "/dev/%s", av[i]);
			fd = open(path, O_RDWR);
			if (fd < 0) {
				fprintf(stderr, "open \"%s\": %s\n",
					path,
					strerror(errno));
			} else {
				rc += cmd(nvmei, av, path + 5, fd);
				close(fd);
			}
			free(path);
		}
	}
	return (rc ? 1 : 0);
}

static
cmd_t
parsecmd(int ac, char **av, int *globokp)
{
	if (ac == 0)
		usage(1);
	if (strcmp(av[0], "info") == 0) {
		*globokp = 1;
		return cmd_info;
	}
	if (strcmp(av[0], "errors") == 0) {
		*globokp = 1;
		return cmd_errors;
	}
	fprintf(stderr, "Command %s not recognized\n", av[0]);

	usage(1);
	return NULL;	/* NOT REACHED */
}

static
int
cmd_info(int ac __unused, char **av __unused, const char *id, int fd)
{
	nvme_getlog_ioctl_t ioc;
	nvme_log_smart_data_t *smart;
	int count;
	int i;

	bzero(&ioc, sizeof(ioc));
	ioc.lid = NVME_LID_SMART;
	ioc.ret_size = sizeof(ioc.info.logsmart);

	if (ioctl(fd, NVMEIOCGETLOG, &ioc) < 0) {
		fprintf(stderr, "ioctl failed: %s\n", strerror(errno));
		return 1;
	}
	if (NVME_COMQ_STATUS_CODE_GET(ioc.status)) {
		fprintf(stderr, "%s: type %d code 0x%02x\n",
			id,
			NVME_COMQ_STATUS_TYPE_GET(ioc.status),
			NVME_COMQ_STATUS_CODE_GET(ioc.status));
		return 1;
	}
	printf("%s:\n", id);
	smart = &ioc.info.logsmart;

	printf("\tcrit_flags:\t");
	if (smart->crit_flags) {
		if (smart->crit_flags & NVME_SMART_CRF_RES80)
			printf(" 80");
		if (smart->crit_flags & NVME_SMART_CRF_RES40)
			printf(" 40");
		if (smart->crit_flags & NVME_SMART_CRF_RES20)
			printf(" 20");
		if (smart->crit_flags & NVME_SMART_CRF_VOLTL_BKUP_FAIL)
			printf(" MEM_BACKUP_FAILED");
		if (smart->crit_flags & NVME_SMART_CRF_MEDIA_RO)
			printf(" MEDIA_RDONLY");
		if (smart->crit_flags & NVME_SMART_CRF_UNRELIABLE)
			printf(" MEDIA_UNRELIABLE");
		if (smart->crit_flags & NVME_SMART_CRF_ABOVE_THRESH)
			printf(" TOO_HOT");
		if (smart->crit_flags & NVME_SMART_CRF_BELOW_THRESH)
			printf(" TOO_COLD");
	} else {
		printf("none\n");
	}
	printf("\tcomp_temp:\t%dC\n",
		(int)(smart->comp_temp1 + (smart->comp_temp2 << 8)) - 273);
	printf("\tLIFE_LEFT:\t%d%% (%d%% used)\n",
		100 - (int)smart->rated_life,
		(int)smart->rated_life);

	printf("\tread_bytes:\t%s\n",
	       format_number(smart->read_count[0] * 512000));
	printf("\twrite_bytes:\t%s\n",
	       format_number(smart->write_count[0] * 512000));
	printf("\tread_cmds:\t%s\n",
	       format_number(smart->read_cmds[0]));
	printf("\twrite_cmds:\t%s\n",
	       format_number(smart->write_cmds[0]));
	printf("\tbusy_time:\t%ld min (%1.2f hrs)\n",
		smart->busy_time[0],
		(double)smart->busy_time[0] / 60.0);
	printf("\tpowon_hours:\t%ld\n", smart->powon_hours[0]);
	printf("\tpower_cyc:\t%ld\n", smart->power_cycles[0]);
	printf("\tunsafe_shut:\t%ld\n", smart->unsafe_shutdowns[0]);

	printf("\tUNRECOV_ERR:\t%ld", smart->unrecoverable_errors[0]);
	if (smart->unrecoverable_errors[0])
		printf("\t*******WARNING*******");
	printf("\n");

	printf("\terr_log_ent:\t%ld\n", smart->error_log_entries[0]);
	printf("\twarn_temp_time:\t%d min (%1.2f hrs)\n",
		smart->warn_comp_temp_time,
		(double)smart->warn_comp_temp_time / 60.0);
	printf("\tcrit_temp_time:\t%d min (%1.2f hrs)\n",
		smart->crit_comp_temp_time,
		(double)smart->crit_comp_temp_time / 60.0);

	printf("\ttemp_sensors:\t");
	for (i = count = 0; i < 8; ++i) {
		if (smart->temp_sensors[i]) {
			if (count)
				printf(" ");
			printf("%dC", smart->temp_sensors[i] - 273);
			++count;
		}
	}
	if (count == 0)
		printf("none");
	printf("\n");

	return 0;
}

static
int
cmd_errors(int ac __unused, char **av __unused, const char *id, int fd)
{
	nvme_getlog_ioctl_t ioc;
	nvme_log_error_data_t *errs;
	int i;

	bzero(&ioc, sizeof(ioc));
	ioc.lid = NVME_LID_ERROR;
	ioc.ret_size = sizeof(ioc.info.logsmart);

	if (ioctl(fd, NVMEIOCGETLOG, &ioc) < 0) {
		fprintf(stderr, "ioctl failed: %s\n", strerror(errno));
		return 1;
	}
	if (NVME_COMQ_STATUS_CODE_GET(ioc.status)) {
		fprintf(stderr, "%s: type %d code 0x%02x\n",
			id,
			NVME_COMQ_STATUS_TYPE_GET(ioc.status),
			NVME_COMQ_STATUS_CODE_GET(ioc.status));
		return 1;
	}
	printf("%s:\n", id);
	errs = &ioc.info.logerr[0];

	for (i = 0; i < 64; ++i) {
		if (errs->error_count == 0 && errs->subq_id == 0 &&
		    errs->cmd_id == 0 && errs->status == 0 &&
		    errs->param == 0 && errs->nsid == 0 &&
		    errs->vendor == 0 && errs->csi == 0 && errs->lba == 0)
			continue;

		if (errs->param || errs->vendor || errs->csi) {
			printf("\t%2d cnt=%-3ld subq=%-2d cmdi=%-3d "
			       "status=%d,0x%02x parm=%04x nsid=%-3d vend=%d "
			       "csi=0x%lx lba=%ld",
			       i, errs->error_count,
			       (int16_t)errs->subq_id,
			       (int16_t)errs->cmd_id,
			       NVME_COMQ_STATUS_TYPE_GET(errs->status),
			       NVME_COMQ_STATUS_CODE_GET(errs->status),
			       errs->param, errs->nsid,
			       errs->vendor,
			       errs->csi, errs->lba);
		} else {
			printf("\t%2d cnt=%-3ld subq=%-2d cmdi=%-3d "
			       "status=%d,0x%02x nsid=%-3d lba=%ld",
			       i, errs->error_count,
			       (int16_t)errs->subq_id,
			       (int16_t)errs->cmd_id,
			       NVME_COMQ_STATUS_TYPE_GET(errs->status),
			       NVME_COMQ_STATUS_CODE_GET(errs->status),
			       errs->nsid,
			       errs->lba);
		}
		if (errs->status & NVME_COMQ_STATUS_DNR)
			printf(" DNR");
		printf(" %s\n", status_to_str(errs->status));
		++errs;
	}

	return 0;
}

static
void
usage(int rc)
{
	fprintf(stderr,
		"nvmectl [-v] cmd [nvme0,1,2...]\n"
		"\tinfo\n"
		"\terrors\n"
	);
	exit(rc);
}
