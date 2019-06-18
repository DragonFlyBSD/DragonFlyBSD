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
 * $FreeBSD: src/sbin/gpt/show.c,v 1.14 2006/06/22 22:22:32 marcel Exp $
 */

#include <sys/types.h>
#include <sys/diskmbr.h>

#include <err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "map.h"
#include "gpt.h"

static bool show_guid = false;
static bool show_label = false;
static bool show_uuid = false;

static void
usage_show(void)
{
	fprintf(stderr,
	    "usage: %s [-glu] device ...\n", getprogname());
	exit(1);
}

static const char *
friendly(uuid_t *t)
{
	static char *save_name1 /*= NULL*/;
	static char *save_name2 /*= NULL*/;

	if (show_uuid)
		goto unfriendly;

	uuid_addr_lookup(t, &save_name1, NULL);
	if (save_name1)
		return (save_name1);

unfriendly:
	if (save_name2) {
		free(save_name2);
		save_name2 = NULL;
	}
	uuid_to_string(t, &save_name2, NULL);
	return (save_name2);
}

static void
show(int fd __unused)
{
	uuid_t type, guid;
	off_t start;
	map_t *m, *p;
	struct mbr *mbr;
	struct gpt_ent *ent;
	unsigned int i;
	char *s;

	printf("  %*s", lbawidth, "start");
	printf("  %*s", lbawidth, "size");
	printf("  index  contents\n");

	m = map_first();
	while (m != NULL) {
		printf("  %*llu", lbawidth, (long long)m->map_start);
		printf("  %*llu", lbawidth, (long long)m->map_size);
		putchar(' ');
		putchar(' ');
		if (m->map_index != NOENTRY)
			printf("%5d", m->map_index);
		else
			printf("    -");
		putchar(' ');
		putchar(' ');
		switch (m->map_type) {
		case MAP_TYPE_UNUSED:
			printf("Unused");
			break;
		case MAP_TYPE_MBR:
			if (m->map_start != 0)
				printf("Extended ");
			printf("MBR");
			break;
		case MAP_TYPE_PRI_GPT_HDR:
			printf("Pri GPT header");
			break;
		case MAP_TYPE_SEC_GPT_HDR:
			printf("Sec GPT header");
			break;
		case MAP_TYPE_PRI_GPT_TBL:
			printf("Pri GPT table");
			break;
		case MAP_TYPE_SEC_GPT_TBL:
			printf("Sec GPT table");
			break;
		case MAP_TYPE_MBR_PART:
			p = m->map_data;
			if (p->map_start != 0)
				printf("Extended ");
			printf("MBR part ");
			mbr = p->map_data;
			for (i = 0; i < 4; i++) {
				start = le16toh(mbr->mbr_part[i].part_start_hi);
				start = (start << 16) +
				    le16toh(mbr->mbr_part[i].part_start_lo);
				if (m->map_start == p->map_start + start)
					break;
			}
			if (i == 4) {
				/* wasn't there */
				printf("[partition not found?]");
			} else {
				printf("%d%s", mbr->mbr_part[i].part_typ,
				    mbr->mbr_part[i].part_flag == 0x80 ?
				    " (active)" : "");
			}
			break;
		case MAP_TYPE_GPT_PART:
			printf("GPT part ");
			ent = m->map_data;
			if (show_label) {
				printf("- \"%s\"",
				    utf16_to_utf8(ent->ent_name));
			} else if (show_guid) {
				s = NULL;
				le_uuid_dec(&ent->ent_uuid, &guid);
				uuid_to_string(&guid, &s, NULL);
				printf("- %s", s);
				free(s);
				s = NULL;
			} else {
				le_uuid_dec(&ent->ent_type, &type);
				printf("- %s", friendly(&type));
			}
			break;
		case MAP_TYPE_PMBR:
			printf("PMBR");
			mbr = m->map_data;
			if (mbr->mbr_part[0].part_typ == DOSPTYP_PMBR &&
			    mbr->mbr_part[0].part_flag == 0x80)
				printf(" (active)");
			break;
		default:
			printf("Unknown %#x", m->map_type);
			break;
		}
		putchar('\n');
		m = m->map_next;
	}
}

int
cmd_show(int argc, char *argv[])
{
	int ch, fd;

	while ((ch = getopt(argc, argv, "glu")) != -1) {
		switch(ch) {
		case 'g':
			show_guid = true;
			break;
		case 'l':
			show_label = true;
			break;
		case 'u':
			show_uuid = true;
			break;
		default:
			usage_show();
		}
	}

	if (argc == optind)
		usage_show();

	while (optind < argc) {
		fd = gpt_open(argv[optind++]);
		if (fd == -1) {
			warn("unable to open device '%s'", device_name);
			continue;
		}

		show(fd);

		gpt_close(fd);
	}

	return (0);
}
