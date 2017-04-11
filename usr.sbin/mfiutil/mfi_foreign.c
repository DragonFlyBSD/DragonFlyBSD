/*-
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mfiutil.h"

MFI_TABLE(top, foreign);

/* We currently don't know the full details of the following struct */
struct mfi_foreign_scan_cfg {
	char data[24];
};

struct mfi_foreign_scan_info {
	uint32_t count; /* Number of foreign configs found */
	struct mfi_foreign_scan_cfg cfgs[8];
};

static int
foreign_drives(int ac __unused, char **av __unused)
{
	struct mfi_pd_info info;
	struct mfi_pd_list *list;
	int error, fd;
	u_int i;
	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	list = NULL;
	if (mfi_pd_get_list(fd, &list, NULL) < 0) {
		error = errno;
		warn("Failed to get drive list");
		goto error;
	}
	/* List the drives. */
	printf("mfi%d Foreign disks:\n", mfi_unit);
	for (i = 0; i < list->count; i++) {
		/* Skip non-hard disks. */
		if (list->addr[i].scsi_dev_type != 0)
			continue;
		/* Fetch details for this drive. */
		if (mfi_pd_get_info(fd, list->addr[i].device_id, &info,
		    NULL) < 0) {
			error = errno;
			warn("Failed to fetch info for drive %u",
			    list->addr[i].device_id);
			goto error;
		}

		if (!info.state.ddf.v.pd_type.is_foreign)
			continue;

		printf("%s ", mfi_drive_name(&info, list->addr[i].device_id,
		    MFI_DNAME_DEVICE_ID));
		print_pd(&info, -1);
		printf(" %s\n", mfi_drive_name(&info, list->addr[i].device_id,
		    MFI_DNAME_ES));
	}
error:
	if(list)
		free(list);
	close(fd);
	error = 0;
	return (0);
}
MFI_COMMAND(foreign, drives, foreign_drives);

static int
foreign_clear(int ac __unused, char **av __unused)
{
	int ch, error, fd;

	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	printf(
	    "Are you sure you wish to clear ALL foreign configurations"
	    " on mfi%u? [y/N] ", mfi_unit);

	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		close(fd);
		return (0);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_CLEAR, NULL, 0, NULL,
	    0, NULL) < 0) {
		error = errno;
		warn("Failed to clear foreign configuration");
		close(fd);
		return (error);
	}

	printf("mfi%d: Foreign configuration cleared\n", mfi_unit);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, clear, foreign_clear);

static int
foreign_scan(int ac __unused, char **av __unused)
{
	struct mfi_foreign_scan_info info;
	int error, fd;

	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	printf("mfi%d: Found %d foreign configurations\n", mfi_unit,
	       info.count);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, scan, foreign_scan);

static int
foreign_show_cfg(int fd, uint32_t opcode, uint8_t cfgidx)
{
	struct mfi_config_data *config;
	int error;
	char *prefix;
	uint8_t mbox[4];

	bzero(mbox, sizeof(mbox));
	mbox[0] = cfgidx;
	if (mfi_config_read_opcode(fd, opcode, &config, mbox, sizeof(mbox))
	    < 0) {
		error = errno;
		warn("Failed to get foreign config %d", cfgidx);
		close(fd);
		return (error);
	}

	if (opcode == MFI_DCMD_CFG_FOREIGN_PREVIEW)
		asprintf(&prefix, "Foreign configuration preview %d", cfgidx);
	else
		asprintf(&prefix, "Foreign configuration %d", cfgidx);
	/*
	 * MegaCli uses DCMD opcodes: 0x03100200 (which fails) followed by
	 * 0x1a721880 which returns what looks to be drive / volume info
	 * but we have no real information on what these are or what they do
	 * so we're currently relying solely on the config returned above
	 */
	dump_config(fd, config, prefix);
	free(config);
	free(prefix);

	return (0);
}

static int
foreign_display(int ac, char **av)
{
	struct mfi_foreign_scan_info info;
	uint8_t i;
	int error, fd;

	if (2 < ac) {
		warnx("foreign display: extra arguments");
                return (EINVAL);
	}

	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	if (0 == info.count) {
		warnx("foreign display: no foreign configs found");
		close(fd);
		return (EINVAL);
	}

	if (1 == ac) {
		for (i = 0; i < info.count; i++) {
			error = foreign_show_cfg(fd,
				MFI_DCMD_CFG_FOREIGN_DISPLAY, i);
			if(0 != error) {
				close(fd);
				return (error);
			}
			if (i < info.count - 1)
				printf("\n");
		}
	} else if (2 == ac) {
		error = foreign_show_cfg(fd,
			MFI_DCMD_CFG_FOREIGN_DISPLAY, atoi(av[1]));
		if (0 != error) {
			close(fd);
			return (error);
		}
	}

	close(fd);
	return (0);
}
MFI_COMMAND(foreign, display, foreign_display);

static int
foreign_preview(int ac, char **av)
{
	struct mfi_foreign_scan_info info;
	uint8_t i;
	int error, fd;

	if (2 < ac) {
		warnx("foreign preview: extra arguments");
                return (EINVAL);
	}

	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	if (0 == info.count) {
		warnx("foreign preview: no foreign configs found");
		close(fd);
		return (EINVAL);
	}

	if (1 == ac) {
		for (i = 0; i < info.count; i++) {
			error = foreign_show_cfg(fd,
				MFI_DCMD_CFG_FOREIGN_PREVIEW, i);
			if(0 != error) {
				close(fd);
				return (error);
			}
			if (i < info.count - 1)
				printf("\n");
		}
	} else if (2 == ac) {
		error = foreign_show_cfg(fd,
			MFI_DCMD_CFG_FOREIGN_PREVIEW, atoi(av[1]));
		if (0 != error) {
			close(fd);
			return (error);
		}
	}

	close(fd);
	return (0);
}
MFI_COMMAND(foreign, preview, foreign_preview);

static int
foreign_import(int ac, char **av)
{
	struct mfi_foreign_scan_info info;
	int ch, error, fd;
	uint8_t cfgidx;
	uint8_t mbox[4];

	if (2 < ac) {
		warnx("foreign preview: extra arguments");
                return (EINVAL);
	}

	fd = mfi_open(mfi_unit);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_SCAN, &info,
	    sizeof(info), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to scan foreign configuration");
		close(fd);
		return (error);
	}

	if (0 == info.count) {
		warnx("foreign import: no foreign configs found");
		close(fd);
		return (EINVAL);
	}

	if (1 == ac) {
		cfgidx = 0xff;
		printf("Are you sure you wish to import ALL foreign "
		       "configurations on mfi%u? [y/N] ", mfi_unit);
	} else {
		/*
		 * While this is docmmented for MegaCli this failed with
		 * exit code 0x03 on the test controller which was a Supermicro
		 * SMC2108 with firmware 12.12.0-0095 which is a LSI 2108 based
		 * controller.
		 */
		cfgidx = atoi(av[1]);
		if (cfgidx >= info.count) {
			warnx("Invalid foreign config %d specified max is %d",
			      cfgidx, info.count - 1);
			close(fd);
			return (EINVAL);
		}
		printf("Are you sure you wish to import the foreign "
		       "configuration %d on mfi%u? [y/N] ", cfgidx, mfi_unit);
	}

	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		close(fd);
		return (0);
	}

	bzero(mbox, sizeof(mbox));
	mbox[0] = cfgidx;
	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_FOREIGN_IMPORT, NULL, 0, mbox,
	    sizeof(mbox), NULL) < 0) {
		error = errno;
		warn("Failed to import foreign configuration");
		close(fd);
		return (error);
	}

	if (1 == ac)
		printf("mfi%d: All foreign configurations imported\n",
		       mfi_unit);
	else
		printf("mfi%d: Foreign configuration %d imported\n", mfi_unit,
		       cfgidx);
	close(fd);
	return (0);
}
MFI_COMMAND(foreign, import, foreign_import);
