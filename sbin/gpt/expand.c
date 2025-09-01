/*-
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpt.h"

static void expand(int fd);

static void
usage_expand(void)
{
	fprintf(stderr, "usage: %s device...\n", getprogname());
	exit(1);
}

int
cmd_expand(int argc, char *argv[])
{
	int ch, fd;

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch(ch) {
		case 'h':
		default:
			usage_expand();
		}
	}

	if (argc == optind)
		usage_expand();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		expand(fd);

		gpt_close(fd);
	}

	return 0;
}

static void
expand(int fd)
{
	map_t *pmbr;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	struct mbr *mbr;
	off_t last;
	off_t last_data;
	off_t blocks;
	off_t delta;
	off_t alignment;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent, *last_ent;
	u_int i, last_i;

	last = mediasz / secsz - 1LL;
	alignment = (off_t)1024 * 1024 / secsz; /* 1MB */

	pmbr = map_find(MAP_TYPE_PMBR);
	if (pmbr == NULL || pmbr->map_start != 0) {
		warnx("%s: error: PMBR not found or invalid", device_name);
		return;
	}

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	if (gpt == NULL) {
		warnx("%s: error: no primary GPT header; run create or recover",
		      device_name);
		return;
	}
	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	if (tbl == NULL) {
		warnx("%s: error: no primary partition table; run recover",
		      device_name);
		return;
	}

	/*
	 * Get the last-positioned partition.
	 */
	hdr = gpt->map_data;
	last_ent = NULL;
	last_i = 0;
	last_data = 0;
	for (i = 0; i < le32toh(hdr->hdr_entries); ++i) {
		ent = (void *)((char *)tbl->map_data + i *
			       le32toh(hdr->hdr_entsz));
		if (!uuid_is_nil(&ent->ent_type, NULL) &&
		    (off_t)le64toh(ent->ent_lba_end) > last_data) {
			last_ent = ent;
			last_i = i;
			last_data = le64toh(ent->ent_lba_end);
		}
	}

	/*
	 * Check the free space.
	 */
	blocks = tbl->map_size;
	if (last_data == 0)
		last_data = tbl->map_start + blocks;
	if (last - blocks <= last_data) {
		warnx("%s: error: no room for secondary GPT table",
		      device_name);
		return;
	}

	/*
	 * Expand the last partition.
	 */
	if (last_ent != NULL) {
		off_t new_size, new_end;

		new_size = last - blocks - le64toh(last_ent->ent_lba_start);
		new_size = new_size / alignment * alignment;
		new_end = le64toh(last_ent->ent_lba_start) + new_size - 1;

		if ((off_t)le64toh(last_ent->ent_lba_end) < new_end) {
			printf("%s: expand entry %d ending %lld => %lld\n",
			       device_name, last_i,
			       (long long)le64toh(last_ent->ent_lba_end),
			       (long long)new_end);
			last_ent->ent_lba_end = htole64(new_end);
		} else if ((off_t)le64toh(last_ent->ent_lba_end) == new_end) {
			printf("%s: entry %d size unchanged\n",
			       device_name, last_i);
		} else {
			warnx("%s: error: cannot shrink entry %d "
			      "(ending %lld => %lld)",
			      device_name, last_i,
			      (long long)le64toh(last_ent->ent_lba_end),
			      (long long)new_end);
			return;
		}
	} else {
		printf("%s: no partition to expand\n", device_name);
	}

	/*
	 * Update the primary GPT header.
	 */
	delta = last - hdr->hdr_lba_alt;
	if (delta == 0) {
		printf("%s: disk size unchanged\n", device_name);
	} else {
		printf("%s: %s GPT by %lld sectors\n", device_name,
		       delta > 0 ? "expand" : "shrink",
		       delta > 0 ? (long long)delta : (long long)-delta);
		hdr->hdr_lba_alt = htole64(last);
	}
	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	/*
	 * Update or create the secondary GPT table.
	 */
	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);
	if (tpg == NULL) {
		warnx("%s: no secondary GPT header; creating\n", device_name);
		tpg = map_add(last, 1LL, MAP_TYPE_SEC_GPT_HDR,
			      calloc(1, secsz));
		memcpy(tpg->map_data, gpt->map_data, secsz);
	}
	if (lbt == NULL) {
		warnx("%s: no secondary GPT table; creating\n", device_name);
		lbt = map_add(last - blocks, blocks, MAP_TYPE_SEC_GPT_TBL,
			      tbl->map_data);
	}
	hdr = tpg->map_data;
	hdr->hdr_lba_self = htole64(tpg->map_start);
	hdr->hdr_lba_table = htole64(lbt->map_start);
	hdr->hdr_lba_alt = htole64(gpt->map_start);
	hdr->hdr_crc_table = htole32(crc32(lbt->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	/*
	 * Update the PMBR.
	 */
	mbr = pmbr->map_data;
	mbr->mbr_part[0].dp_ehd = 0xff;
	mbr->mbr_part[0].dp_esect = 0xff;
	mbr->mbr_part[0].dp_ecyl = 0xff;
	if (last > 0xffffffff)
		mbr->mbr_part[0].dp_size = htole32(0xffffffffU);
	else
		mbr->mbr_part[0].dp_size = htole32((uint32_t)last);

	/*
	 * Write out.
	 *
	 * NOTE! We don't even try to manage the in-memory media links
	 *	 and tbl2's data is the same pointer as tbl's data.  Don't
	 *	 try to clean up here or be fancy.
	 */
	gpt_write(fd, lbt);	/* secondary partition table */
	gpt_write(fd, tpg);	/* secondary header */
	gpt_write(fd, gpt);	/* primary partition table */
	gpt_write(fd, tbl);	/* primary header */
	gpt_write(fd, pmbr);
}
