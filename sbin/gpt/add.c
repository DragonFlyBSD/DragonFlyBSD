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
 * $FreeBSD: src/sbin/gpt/add.c,v 1.15 2006/10/04 18:20:25 marcel Exp $
 */

#include <sys/param.h>

#include <err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpt.h"

static uuid_t type;
static off_t block, size, alignment;
static unsigned int entry = MAP_NOENTRY;
static const char *name;

static void
usage_add(void)
{
	fprintf(stderr,
		"usage: %s [-a alignment] [-b block] [-i index] [-l label] "
		"[-s size] [-t uuid/alias] device ...\n",
		getprogname());
	exit(1);
}

static void
add(int fd)
{
	map_t *gpt, *tpg;
	map_t *tbl, *lbt;
	map_t *map;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	unsigned int i;

	if (alignment == 0)
		alignment = (off_t)1024 * 1024 / secsz; /* 1MB */

	gpt = map_find(MAP_TYPE_PRI_GPT_HDR);
	if (gpt == NULL) {
		warnx("%s: error: no primary GPT header; run create or recover",
		    device_name);
		return;
	}

	tpg = map_find(MAP_TYPE_SEC_GPT_HDR);
	if (tpg == NULL) {
		warnx("%s: error: no secondary GPT header; run recover",
		    device_name);
		return;
	}

	tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	lbt = map_find(MAP_TYPE_SEC_GPT_TBL);
	if (tbl == NULL || lbt == NULL) {
		warnx("%s: error: run recover -- trust me", device_name);
		return;
	}

	hdr = gpt->map_data;
	if (entry != MAP_NOENTRY && entry > le32toh(hdr->hdr_entries)) {
		warnx("%s: error: index %u out of range (%u max)", device_name,
		    entry, le32toh(hdr->hdr_entries));
		return;
	}

	if (entry != MAP_NOENTRY) {
		i = entry;
		ent = (void *)((char *)tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		if (!uuid_is_nil(&ent->ent_type, NULL)) {
			warnx("%s: error: entry at index %u is not free",
			    device_name, entry);
			return;
		}
	} else {
		/* Find empty slot in GPT table. */
		ent = NULL;
		for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
			ent = (void *)((char *)tbl->map_data + i *
			    le32toh(hdr->hdr_entsz));
			if (uuid_is_nil(&ent->ent_type, NULL))
				break;
		}
		if (i == le32toh(hdr->hdr_entries)) {
			warnx("%s: error: no available table entries",
			    device_name);
			return;
		}
	}

	map = map_alloc(block, size, alignment);
	if (map == NULL) {
		warnx("%s: error: no space available on device", device_name);
		return;
	}

	uuid_enc_le(&ent->ent_type, &type);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);
	if (name != NULL)
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

	hdr->hdr_crc_table = htole32(crc32(tbl->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, gpt);
	gpt_write(fd, tbl);

	hdr = tpg->map_data;
	ent = (void*)((char*)lbt->map_data + i * le32toh(hdr->hdr_entsz));

	uuid_enc_le(&ent->ent_type, &type);
	ent->ent_lba_start = htole64(map->map_start);
	ent->ent_lba_end = htole64(map->map_start + map->map_size - 1LL);
	if (name != NULL)
		utf8_to_utf16(name, ent->ent_name, NELEM(ent->ent_name));

	hdr->hdr_crc_table = htole32(crc32(lbt->map_data,
	    le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz)));
	hdr->hdr_crc_self = 0;
	hdr->hdr_crc_self = htole32(crc32(hdr, le32toh(hdr->hdr_size)));

	gpt_write(fd, lbt);
	gpt_write(fd, tpg);

	printf("%ss%u added\n", device_name, i);
}

void
add_defaults(int fd)
{
	entry = 0;
	size = (off_t)256 * 1024 * 1024 / secsz; /* 256MB */
	if (parse_uuid("EFI System", &type) != 0) {
		fprintf(stderr, "Unable to lookup uuid 'EFI System'\n");
		exit(1);
	}
	add(fd);

	entry = 1;
	size = 0; /* all free space */
	if (parse_uuid("DragonFly Label64", &type) != 0) {
		fprintf(stderr, "Unable to lookup uuid 'DragonFly Label64'\n");
		exit(1);
	}
	add(fd);
}

static int64_t
parse_number(const char *s)
{
	int64_t v;
	char *suffix;

	v = (int64_t)strtol(s, &suffix, 0);
	if (suffix == s)
		return (-1);
	if (*suffix == '\0')
		return (v);
	if (suffix[1] != '\0')
		return (-1);

	switch (*suffix) {
	case 'g':
	case 'G':
		v *= 1024;
		/* FALLTHROUGH */
	case 'm':
	case 'M':
		v *= 1024;
		/* FALLTHROUGH */
	case 'k':
	case 'K':
		v *= 1024;
		break;
	default:
		return (-1);
	}

	return (v);
}

static int64_t
parse_size(const char *s, bool *is_sector)
{
	int64_t v;
	char *suffix;

	v = (int64_t)strtol(s, &suffix, 0);
	if (suffix == s)
		return (-1);
	if (*suffix == '\0') {
		*is_sector = true;
		return (v);
	}
	if (suffix[1] != '\0')
		return (-1);

	switch (*suffix) {
	case 's':
	case 'S':
		*is_sector = true;
		break;
	case 'p':
	case 'P':
		v *= 1024;
		/* FALLTHROUGH */
	case 't':
	case 'T':
		v *= 1024;
		/* FALLTHROUGH */
	case 'g':
	case 'G':
		v *= 1024;
		/* FALLTHROUGH */
	case 'm':
	case 'M':
		v *= 1024;
		/* FALLTHROUGH */
	case 'k':
	case 'K':
		v *= 1024;
		/* FALLTHROUGH */
	case 'b':
	case 'B':
		*is_sector = false;
		break;
	default:
		return (-1);
	}

	return (v);
}

int
cmd_add(int argc, char *argv[])
{
	char *p;
	int ch, fd;
	bool is_sector;
	int64_t v;

	is_sector = false;
	while ((ch = getopt(argc, argv, "a:b:hi:l:s:t:")) != -1) {
		switch(ch) {
		case 'a':
			if (alignment > 0)
				errx(1, "-a alignment already specified");
			v = parse_number(optarg);
			if (v < 0)
				errx(1, "invalid alignment: %s", optarg);
			alignment = (off_t)v;
			break;
		case 'b':
			if (block > 0)
				errx(1, "-b block already specified");
			block = strtoll(optarg, &p, 10);
			if (*p != 0 || block < 1)
				errx(1, "invalid block: %s", optarg);
			break;
		case 'i':
			if (entry != MAP_NOENTRY)
				errx(1, "-i index already specified");
			entry = strtoul(optarg, &p, 10);
			if (*p != 0 || entry == MAP_NOENTRY)
				errx(1, "invalid entry: %s", optarg);
			break;
		case 'l':
			name = optarg;
			break;
		case 's':
			if (size > 0)
				errx(1, "-s size already specified");
			v = parse_size(optarg, &is_sector);
			if (v < 0)
				errx(1, "invalid size: %s", optarg);
			size = (off_t)v;
			break;
		case 't':
			if (!uuid_is_nil(&type, NULL))
				errx(1, "-t type already specified");
			if (parse_uuid(optarg, &type) != 0)
				errx(1, "invalid type: %s", optarg);
			break;
		case 'h':
		default:
			usage_add();
		}
	}

	if (argc == optind)
		usage_add();

	if (uuid_is_nil(&type, NULL)) {
		if (parse_uuid("DragonFly Label64", &type) != 0)
			err(1, "unable to find uuid for 'DragonFly Label64'");
	}

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		if (alignment > 0) {
			if (alignment % secsz != 0)
				warnx("alignment (%lld) not multiple of "
				      "sector size (%u)",
				      (long long)alignment, secsz);
			alignment = (alignment + secsz - 1) / secsz;
		}
		if (size > 0 && !is_sector) {
			if (size % secsz != 0)
				warnx("size (%lld) not multiple of "
				      "sector size (%u)",
				      (long long)size, secsz);
			size = (size + secsz - 1) / secsz;
		}

		add(fd);

		gpt_close(fd);
	}

	return (0);
}
