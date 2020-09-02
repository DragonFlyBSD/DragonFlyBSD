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

#include "map.h"
#include "gpt.h"

static void expand(int fd);

int
cmd_expand(int argc, char *argv[])
{
	int fd;

	if (argc == optind) {
		fprintf(stderr, "usage: gpt expand <device>...\n");
		exit(1);
	}
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
expand(int fd __unused)
{
	map_t *pmbr;
	map_t *gpt, *gpt2;
	map_t *tbl, *tbl2;
	map_t *map __unused;
	struct mbr *mbr;
	off_t last;
	off_t blocks;
	off_t delta;
	off_t nblocks;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	struct gpt_ent *lent;
	char *name = NULL;
	u_int i;
	u_int li;

	pmbr = map_find(MAP_TYPE_PMBR);
	if (pmbr == NULL) {
		warnx("%s: error: no PMBR to expand", device_name);
		return;
	}

	mbr = pmbr->map_data;

	last = mediasz / secsz - 1LL;

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	if (gpt == NULL) {
		warnx("%s: error: no primary GPT header; run create or recover",
		    device_name);
		return;
	}
	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	if (tbl == NULL) {
		warnx("%s: error: no primary partition table; "
		      "run create or recover",
		      device_name);
		return;
	}
	blocks = tbl->map_size;

	/*
	 * Since the device may have changed size, gpt might not be able to
	 * find the backup table.  Just ignore anytying we scanned and
	 * create new maps for the secondary gpt and table.
	 */
	gpt2 = mkmap(last, 1LL, MAP_TYPE_SEC_GPT_HDR);
	gpt2->map_data = calloc(1, secsz);

	tbl2 = mkmap(last - blocks, blocks, MAP_TYPE_SEC_GPT_TBL);
	tbl2->map_data = tbl->map_data;

	/*
	 * Update the PMBR
	 */
	if (last > 0xffffffff) {
		mbr->mbr_part[0].part_size_lo = htole16(0xffff);
		mbr->mbr_part[0].part_size_hi = htole16(0xffff);
	} else {
		mbr->mbr_part[0].part_size_lo = htole16(last);
		mbr->mbr_part[0].part_size_hi = htole16(last >> 16);
	}

	/*
	 * Calculate expansion size
	 *
	 * Update the primary gpt header, adjusting the pointer to the
	 * alternative table.
	 */
	hdr = gpt->map_data;
	delta = last - hdr->hdr_lba_alt;
	hdr->hdr_lba_alt = htole64(last);

	/*
	 * Update the secondary gpt header.
	 */
	if (delta == 0) {
		printf("gpt already expanded to full device size\n");
	} else {
		printf("Expand GPT by %jd blocks\n", (intmax_t)delta);
	}

	/*
	 * Create the secondary gpt header
	 */
	hdr = gpt2->map_data;
	*hdr = *(struct gpt_hdr *)gpt->map_data;

	hdr->hdr_lba_self = htole64(gpt2->map_start);
	hdr->hdr_lba_table = htole64(tbl2->map_start);
	hdr->hdr_lba_alt = htole64(1);

	lent = NULL;
	li = 0;
	for (i = 0; i < le32toh(hdr->hdr_entries); ++i) {
		ent = (void *)((char *)tbl->map_data + i *
			       le32toh(hdr->hdr_entsz));
		if (uuid_is_nil(&ent->ent_type, NULL))
			continue;
		lent = ent;
		li = i;
	}

	hdr = gpt2->map_data;

	if (lent) {
		uuid_to_string(&ent->ent_type, &name, NULL);
		nblocks = last - blocks - le64toh(lent->ent_lba_start);
		nblocks = (nblocks * secsz / (1024 * 1024)) * 1024 * 1024 /
			  secsz;

		if (le64toh(lent->ent_lba_end) ==
		    le64toh(lent->ent_lba_start) + nblocks - 1) {
			printf("entry %d type=%s %ld,%ld unchanged\n",
				li, name,
				le64toh(lent->ent_lba_start),
				le64toh(lent->ent_lba_end));
		} else {
			printf("expand entry %d type=%s %ld,%ld to %ld\n",
				li, name,
				le64toh(lent->ent_lba_start),
				le64toh(lent->ent_lba_end),
				le64toh(lent->ent_lba_start) + nblocks - 1);
			lent->ent_lba_end = htole64(
						le64toh(lent->ent_lba_start) +
						nblocks - 1);
		}
	}

	/*
	 * Write out
	 */
	hdr = gpt->map_data;
	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
		le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	hdr = gpt2->map_data;
	hdr->hdr_crc_table = htole32(crc32(tbl2->map_data,
		le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	/*
	 * NOTE! We don't even try to manage the in-memory media links
	 *	 and tbl2's data is the same pointer as tbl's data.  Don't
	 *	 try to clean up here or be fancy.
	 */
	gpt_write(fd, tbl2);	/* secondary partition table */
	gpt_write(fd, gpt2);	/* secondary header */
	gpt_write(fd, gpt);	/* primary partition table */
	gpt_write(fd, tbl);	/* primary header */
	gpt_write(fd, pmbr);	/* primary header */

	if (name)
		free(name);
}
