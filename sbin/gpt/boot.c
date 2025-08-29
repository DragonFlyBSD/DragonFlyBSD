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
 */

#include <sys/types.h>

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "gpt.h"

static void
usage_boot(void)
{
	fprintf(stderr, "usage: %s device\n", getprogname());
	exit(1);
}

static void
bootset(int fd)
{
	off_t  block;
	off_t  size;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	struct mbr *mbr;
	int bfd;

	/*
	 * Paramters for boot partition (entry #0)
	 */
	uuid_t uuid = GPT_ENT_TYPE_DRAGONFLY_LABEL32;
	block = 0;
	size = (off_t)1024 * 1024 * 1024 / secsz;		/* 1GB */

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	if (gpt == NULL)
		errx(1, "%s: error: no primary GPT header", device_name);
	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	if (tpg == NULL)
		errx(1, "%s: error: no secondary GPT header", device_name);
	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);
	if (tbl == NULL || lbt == NULL) {
		errx(1, "%s: error: no primary or secondary GPT table",
		     device_name);
	}

	hdr = gpt->map_data;
	ent = tbl->map_data; /* entry #0 */
	if (!uuid_is_nil(&ent->ent_type, NULL))
		errx(1, "%s: error: entry #0 is not free", device_name);
	map = map_alloc(block, size);
	if (map == NULL)
		errx(1, "%s: error: no space available on device", device_name);
	block = map->map_start;
	size  = map->map_size;

	/*
	 * Write primary GPT table
	 */
	uuid_enc_le(&ent->ent_type, &uuid);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);

	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
				     le32toh(hdr->hdr_entries) *
				     le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, gpt);
	gpt_write(fd, tbl);

	/*
	 * Write secondary GPT table
	 */
	hdr = tpg->map_data;
	ent = lbt->map_data; /* entry #0 */
	uuid_enc_le(&ent->ent_type, &uuid);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);

	hdr->hdr_crc_table = htole32(crc32(lbt->map_data,
				     le32toh(hdr->hdr_entries) *
				     le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, lbt);
	gpt_write(fd, tpg);

	/*
	 * Create a dummy partition
	 */
	map = map_find(MAP_TYPE_PMBR);
	if (map == NULL)
		errx(1, "I can't find the PMBR!");
	mbr = map->map_data;
	if (mbr == NULL)
		errx(1, "I can't find the PMBR's data!");

	/*
	 * Copy in real boot code
	 */
	bfd = open("/boot/boot0", O_RDONLY);
	if (bfd < 0 ||
	    read(bfd, mbr->mbr_code, sizeof(mbr->mbr_code)) !=
	    sizeof(mbr->mbr_code)) {
		errx(1, "Cannot read /boot/boot0");
	}
	close(bfd);

	/*
	 * Generate partition #1
	 */
	mbr->mbr_part[1].dp_shd = 0xff;
	mbr->mbr_part[1].dp_ssect = 0xff;
	mbr->mbr_part[1].dp_scyl = 0xff;
	mbr->mbr_part[1].dp_ehd = 0xff;
	mbr->mbr_part[1].dp_esect = 0xff;
	mbr->mbr_part[1].dp_ecyl = 0xff;
	mbr->mbr_part[1].dp_start = htole32((uint32_t)block);
	mbr->mbr_part[1].dp_size = htole32((uint32_t)size);
	mbr->mbr_part[1].dp_typ = DOSPTYP_DFLYBSD;
	mbr->mbr_part[1].dp_flag = 0x80;

	gpt_write(fd, map);
}

int
cmd_boot(int argc, char *argv[])
{
	int ch, fd;

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch(ch) {
		case 'h':
		default:
			usage_boot();
		}
	}

	if (argc == optind)
		usage_boot();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}
		bootset(fd);
		gpt_close(fd);
	}
	return (0);
}

