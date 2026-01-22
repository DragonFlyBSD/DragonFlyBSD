/*-
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
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
 *
 * $FreeBSD: src/sbin/gpt/migrate.c,v 1.16 2005/09/01 02:42:52 marcel Exp $
 */

#include <sys/param.h>
#include <sys/disklabel32.h>
#include <sys/disklabel64.h>
#include <sys/dtype.h>

#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpt.h"

static int force;
static int slice;
static uint32_t parts;

static void
usage_migrate(void)
{
	fprintf(stderr,
		"usage: %s [-fs] [-p nparts] device ...\n",
		getprogname());
	exit(1);
}

static int
read_disklabel32(int fd, off_t start, struct disklabel32 **dlp)
{
	char *buf;
	struct disklabel32 *dl;

	buf = gpt_read(fd, start + LABELSECTOR32, 1);
	if (buf == NULL) {
		warnx("%s: error: reading disklabel32 failed", device_name);
		return (EIO);
	}

	dl = (void *)(buf + LABELOFFSET32);
	if (le32toh(dl->d_magic) != DISKMAGIC32 ||
	    le32toh(dl->d_magic2) != DISKMAGIC32) {
		warnx("%s: warning: no disklabel32 in slice", device_name);
		free(buf);
		return (ENOENT);
	}

	*dlp = dl;
	return 0;
}

static int
read_disklabel64(int fd, off_t start, struct disklabel64 **dlp)
{
	struct disklabel64 *dl;

	dl = gpt_read(fd, start, roundup2(sizeof(*dl), secsz) / secsz);
	if (dl == NULL) {
		warnx("%s: error: reading disklabel64 failed", device_name);
		return (EIO);
	}

	if (le32toh(dl->d_magic) != DISKMAGIC64) {
		warnx("%s: warning: no disklabel64 in slice", device_name);
		free(dl);
		return (ENOENT);
	}

	*dlp = dl;
	return 0;
}

static int
convert_fstype(uint8_t fstype, uuid_t *uuid)
{
	switch (fstype) {
	case FS_UNUSED:
		uuid_create_nil(uuid, NULL);
		return 0;
	case FS_SWAP:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_SWAP;
		return 0;
	case FS_BSDFFS:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_UFS1;
		return 0;
	case FS_VINUM:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_VINUM;
		return 0;
	case FS_CCD:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_CCD;
		return 0;
	case FS_HAMMER:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_HAMMER;
		return 0;
	case FS_HAMMER2:
		*uuid = (uuid_t)GPT_ENT_TYPE_DRAGONFLY_HAMMER2;
		return 0;
	default:
		uuid_create_nil(uuid, NULL);
		return (-1);
	}
}

static struct gpt_ent*
migrate_disklabel32(const struct disklabel32 *dl, off_t start,
		    struct gpt_ent *ent)
{
	uuid_t uuid;
	off_t ofs, rawofs;
	int i;

	/*
	 * If any partition starts before RAW_PART, then RAW_PART is not acting
	 * as a base coordinate; i.e., partition offsets are already absolute.
	 */
	rawofs = le32toh(dl->d_partitions[RAW_PART].p_offset) *
	    le32toh(dl->d_secsize);
	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		if (dl->d_partitions[i].p_fstype == FS_UNUSED)
			continue;
		ofs = le32toh(dl->d_partitions[i].p_offset) *
		    le32toh(dl->d_secsize);
		if (ofs < rawofs)
			rawofs = 0;
	}
	rawofs /= secsz;

	for (i = 0; i < le16toh(dl->d_npartitions); i++) {
		if (convert_fstype(dl->d_partitions[i].p_fstype, &uuid) < 0) {
			warnx("%s: %s: unknown partition type (%d)",
			      device_name, (force ? "warning" : "error"),
			      dl->d_partitions[i].p_fstype);
			if (!force)
				return (NULL);
		}
		if (uuid_is_nil(&uuid, NULL))
			continue;

		uuid_enc_le(&ent->ent_type, &uuid);

		ofs = (le32toh(dl->d_partitions[i].p_offset) *
		    le32toh(dl->d_secsize)) / secsz;
		ofs = (ofs > 0) ? ofs - rawofs : 0;
		ent->ent_lba_start = htole64(start + ofs);
		ent->ent_lba_end = htole64(start + ofs +
		    le32toh(dl->d_partitions[i].p_size) - 1LL);

		ent++;
	}

	return (ent);
}

static struct gpt_ent*
migrate_disklabel64(const struct disklabel64 *dl, off_t start,
		    struct gpt_ent *ent)
{
	uuid_t uuid;
	off_t offset, blocks;
	uint32_t i;

	for (i = 0; i < le32toh(dl->d_npartitions); i++) {
		if (convert_fstype(dl->d_partitions[i].p_fstype, &uuid) < 0) {
			warnx("%s: %s: unknown partition type (%d)",
			      device_name, (force ? "warning" : "error"),
			      dl->d_partitions[i].p_fstype);
			if (!force)
				return (NULL);
		}
		if (uuid_is_nil(&uuid, NULL))
			continue;

		uuid_enc_le(&ent->ent_type, &uuid);

		offset = le64toh(dl->d_partitions[i].p_boffset) / secsz;
		blocks = le64toh(dl->d_partitions[i].p_bsize) / secsz;
		ent->ent_lba_start = htole64(start + offset);
		ent->ent_lba_end = htole64(start + offset + blocks - 1LL);

		ent++;
	}

	return (ent);
}

static void
migrate(int fd)
{
	uuid_t uuid;
	off_t blocks, last;
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	struct mbr *mbr;
	uint32_t start, size;
	unsigned int i;

	last = mediasz / secsz - 1LL;

	map = map_find(MAP_TYPE_MBR);
	if (map == NULL || map->map_start != 0) {
		warnx("%s: error: no partitions to convert", device_name);
		return;
	}

	mbr = map->map_data;

	if (map_find(MAP_TYPE_PRI_GPT_HDR) != NULL ||
	    map_find(MAP_TYPE_SEC_GPT_HDR) != NULL) {
		warnx("%s: error: device already contains a GPT", device_name);
		return;
	}

	/* Get the amount of free space after the MBR */
	blocks = map_free(1LL);
	if (blocks == 0LL) {
		warnx("%s: error: no room for the GPT header", device_name);
		return;
	}

	/* Don't create more than parts entries. */
	if ((uint64_t)(blocks - 1) * secsz > parts * sizeof(struct gpt_ent)) {
		blocks = (parts * sizeof(struct gpt_ent)) / secsz;
		if ((parts * sizeof(struct gpt_ent)) % secsz)
			blocks++;
		blocks++;		/* Don't forget the header itself */
	}

	/* Never cross the median of the device. */
	if ((blocks + 1LL) > ((last + 1LL) >> 1))
		blocks = ((last + 1LL) >> 1) - 1LL;

	/*
	 * Get the amount of free space at the end of the device and
	 * calculate the size for the GPT structures.
	 */
	map = map_last();
	if (map->map_type != MAP_TYPE_UNUSED) {
		warnx("%s: error: no room for the backup header", device_name);
		return;
	}

	if (map->map_size < blocks)
		blocks = map->map_size;
	if (blocks == 1LL) {
		warnx("%s: error: no room for the GPT table", device_name);
		return;
	}

	blocks--;		/* Number of blocks in the GPT table. */
	gpt = map_add(1LL, 1LL, MAP_TYPE_PRI_GPT_HDR, calloc(1, secsz));
	tbl = map_add(2LL, blocks, MAP_TYPE_PRI_GPT_TBL,
	    calloc(blocks, secsz));
	if (gpt == NULL || tbl == NULL)
		return;

	lbt = map_add(last - blocks, blocks, MAP_TYPE_SEC_GPT_TBL,
	    tbl->map_data);
	tpg = map_add(last, 1LL, MAP_TYPE_SEC_GPT_HDR, calloc(1, secsz));

	hdr = gpt->map_data;
	memcpy(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig));
	hdr->hdr_revision = htole32(GPT_HDR_REVISION);
	hdr->hdr_size = htole32(GPT_MIN_HDR_SIZE);
	hdr->hdr_lba_self = htole64(gpt->map_start);
	hdr->hdr_lba_alt = htole64(tpg->map_start);
	hdr->hdr_lba_start = htole64(tbl->map_start + blocks);
	hdr->hdr_lba_end = htole64(lbt->map_start - 1LL);
	uuid_create(&uuid, NULL);
	uuid_enc_le(&hdr->hdr_uuid, &uuid);
	hdr->hdr_lba_table = htole64(tbl->map_start);
	hdr->hdr_entries = htole32((blocks * secsz) / sizeof(struct gpt_ent));
	if (le32toh(hdr->hdr_entries) > parts)
		hdr->hdr_entries = htole32(parts);
	hdr->hdr_entsz = htole32(sizeof(struct gpt_ent));

	ent = tbl->map_data;
	for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
		uuid_create(&uuid, NULL);
		uuid_enc_le(&ent[i].ent_uuid, &uuid);
	}

	/* Mirror partitions. */
	for (i = 0; i < 4; i++) {
		start = le32toh(mbr->mbr_part[i].dp_start);
		size = le32toh(mbr->mbr_part[i].dp_size);

		switch (mbr->mbr_part[i].dp_typ) {
		case 0:
			break;
		case DOSPTYP_DFLYBSD:
		case DOSPTYP_386BSD: {
			struct disklabel32 *dl32 = NULL;
			struct disklabel64 *dl64 = NULL;
			int err;

			err = read_disklabel64(fd, start, &dl64);
			if (err == ENOENT)
				err = read_disklabel32(fd, start, &dl32);
			if (err == ENOENT)
				break;
			else if (err != 0)
				return;

			if (slice) {
				if (dl64 != NULL) {
					uuid = (uuid_t)
					    GPT_ENT_TYPE_DRAGONFLY_LABEL64;
				} else {
					uuid = (uuid_t)
					    GPT_ENT_TYPE_DRAGONFLY_LABEL32;
				}
				uuid_enc_le(&ent->ent_type, &uuid);
				ent->ent_lba_start = htole64((uint64_t)start);
				ent->ent_lba_end = htole64(start + size - 1LL);
				ent++;
			} else if (dl64 != NULL) {
				ent = migrate_disklabel64(dl64, start, ent);
			} else {
				ent = migrate_disklabel32(dl32, start, ent);
			}
			free(dl64);
			free(dl32);
			if (ent == NULL)
				return;
			break;
		}
		case DOSPTYP_EFI: {
			uuid_t efi_slice = GPT_ENT_TYPE_EFI;
			uuid_enc_le(&ent->ent_type, &efi_slice);
			ent->ent_lba_start = htole64((uint64_t)start);
			ent->ent_lba_end = htole64(start + size - 1LL);
			ent++;
			break;
		}
		default:
			warnx("%s: %s: partition %d: unknown type (%d)",
			      device_name, (force ? "warning" : "error"),
			      i, mbr->mbr_part[i].dp_typ);
			if (!force)
				return;
		}
	}

	ent = tbl->map_data;
	hdr->hdr_crc_table = htole32(crc32(ent, le32toh(hdr->hdr_entries) *
	    le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, gpt);
	gpt_write(fd, tbl);

	/*
	 * Create backup GPT.
	 */
	memcpy(tpg->map_data, gpt->map_data, secsz);
	hdr = tpg->map_data;
	hdr->hdr_lba_self = htole64(tpg->map_start);
	hdr->hdr_lba_alt = htole64(gpt->map_start);
	hdr->hdr_lba_table = htole64(lbt->map_start);
	hdr->hdr_crc_self = 0;			/* Don't ever forget this! */
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, lbt);
	gpt_write(fd, tpg);

	map = map_find(MAP_TYPE_MBR);
	mbr = map->map_data;
	/*
	 * Turn the MBR into a Protective MBR.
	 */
	bzero(mbr->mbr_part, sizeof(mbr->mbr_part));
	mbr->mbr_part[0].dp_shd = 0xff;
	mbr->mbr_part[0].dp_ssect = 0xff;
	mbr->mbr_part[0].dp_scyl = 0xff;
	mbr->mbr_part[0].dp_typ = DOSPTYP_PMBR;
	mbr->mbr_part[0].dp_ehd = 0xff;
	mbr->mbr_part[0].dp_esect = 0xff;
	mbr->mbr_part[0].dp_ecyl = 0xff;
	mbr->mbr_part[0].dp_start = htole32(1U);
	if (last > 0xffffffff)
		mbr->mbr_part[0].dp_size = htole32(0xffffffffU);
	else
		mbr->mbr_part[0].dp_size = htole32((uint32_t)last);
	gpt_write(fd, map);
}

int
cmd_migrate(int argc, char *argv[])
{
	char *p;
	int ch, fd;

	/* Get the migrate options */
	while ((ch = getopt(argc, argv, "fhps")) != -1) {
		switch(ch) {
		case 'f':
			force = 1;
			break;
		case 'p':
			parts = (uint32_t)strtol(optarg, &p, 10);
			if (*p != 0 || parts == 0)
				usage_migrate();
			break;
		case 's':
			slice = 1;
			break;
		case 'h':
		default:
			usage_migrate();
		}
	}
	if (parts == 0)
		parts = 128;

	if (argc == optind)
		usage_migrate();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		migrate(fd);

		gpt_close(fd);
	}

	return (0);
}
