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
 * CRC32 code derived from work by Gary S. Brown.
 *
 * $FreeBSD: src/sbin/gpt/gpt.c,v 1.16 2006/07/07 02:44:23 marcel Exp $
 */

#include <sys/param.h>
#include <sys/diskslice.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpt.h"

char	*device_name;
off_t	mediasz;
u_int	secsz;
bool	readonly;
int	verbose;

static uint32_t crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t
crc32(const void *buf, size_t size)
{
	const uint8_t *p;
	uint32_t crc;

	p = buf;
	crc = ~0U;

	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return crc ^ ~0U;
}

/*
 * Produce a NUL-terminated UTF-8 string from the non-NUL-terminated
 * UTF-16LE string.  The destination buffer (s8) is suggested to have
 * a size of (s16len * 3 + 1).
 */
void
utf16_to_utf8(const uint16_t *s16, size_t s16len, char *s8, size_t s8len)
{
	size_t s8idx, s16idx;
	uint32_t utfchar, low;
	unsigned char *us8 = (unsigned char *)s8;

	s8idx = s16idx = 0;
	while (s16idx < s16len && s16[s16idx] != 0) {
		utfchar = le16toh(s16[s16idx++]);

		/* Handle surrogate pairs */
		if (utfchar >= 0xd800 && utfchar <= 0xdbff) {
			/* high surrogate */
			if (s16idx < s16len) {
				low = le16toh(s16[s16idx]);
				if (low >= 0xdc00 && low <= 0xdfff) {
					s16idx++;
					utfchar = 0x10000 +
					    ((utfchar - 0xd800) << 10) +
					    (low - 0xdc00);
				} else {
					utfchar = 0xfffd;
				}
			} else {
				utfchar = 0xfffd;
			}
		} else if (utfchar >= 0xdc00 && utfchar <= 0xdfff) {
			/* lone low surrogate */
			utfchar = 0xfffd;
		}

		/* Encode as UTF-8 */
		if (utfchar < 0x80) {
			if (s8idx + 1 >= s8len)
				break;
			us8[s8idx++] = utfchar;
		} else if (utfchar < 0x800) {
			if (s8idx + 2 >= s8len)
				break;
			us8[s8idx++] = 0xc0 | (utfchar >> 6);
			us8[s8idx++] = 0x80 | (utfchar & 0x3f);
		} else if (utfchar < 0x10000) {
			if (s8idx + 3 >= s8len)
				break;
			us8[s8idx++] = 0xe0 | (utfchar >> 12);
			us8[s8idx++] = 0x80 | ((utfchar >> 6) & 0x3f);
			us8[s8idx++] = 0x80 | (utfchar & 0x3f);
		} else {
			if (s8idx + 4 >= s8len)
				break;
			us8[s8idx++] = 0xf0 | (utfchar >> 18);
			us8[s8idx++] = 0x80 | ((utfchar >> 12) & 0x3f);
			us8[s8idx++] = 0x80 | ((utfchar >> 6) & 0x3f);
			us8[s8idx++] = 0x80 | (utfchar & 0x3f);
		}
	}

	us8[s8idx] = 0;
}

/*
 * Produce a non-NUL-terminated UTF-16LE string from the NUL-terminated
 * UTF-8 string.
 */
void
utf8_to_utf16(const char *s8, uint16_t *s16, size_t s16len)
{
	size_t s16idx, s8idx;
	uint32_t utfchar;
	uint8_t c, utfbytes;
	const unsigned char *us8 = (const unsigned char *)s8;

	s8idx = s16idx = 0;
	utfchar = 0;
	while ((c = us8[s8idx++]) != 0) {
		if ((c & 0x80) == 0x00) {
			/* ASCII */
			utfchar = c;
			utfbytes = 0;
		} else if ((c & 0xe0) == 0xc0) {
			utfchar = c & 0x1f;
			utfbytes = 1;
		} else if ((c & 0xf0) == 0xe0) {
			utfchar = c & 0x0f;
			utfbytes = 2;
		} else if ((c & 0xf8) == 0xf0) {
			utfchar = c & 0x07;
			utfbytes = 3;
		} else {
			/* Invalid leading byte */
			utfchar = 0xfffd;
			utfbytes = 0;
		}

		while (utfbytes-- > 0) {
			c = us8[s8idx++];
			if ((c & 0xc0) != 0x80) {
				utfchar = 0xfffd;
				break;
			}
			utfchar = (utfchar << 6) | (c & 0x3f);
		}

		/* Reject invalid Unicode scalar values */
		if (utfchar > 0x10ffff ||
		    (utfchar >= 0xd800 && utfchar <= 0xdfff)) {
			utfchar = 0xfffd;
		}

		if (utfchar < 0x10000) {
			if (s16idx + 1 > s16len)
				break;
			s16[s16idx++] = htole16((uint16_t)utfchar);
		} else {
			/* Surrogate pair */
			if (s16idx + 2 > s16len)
				break;
			utfchar -= 0x10000;
			s16[s16idx++] = htole16(0xd800 | (utfchar >> 10));
			s16[s16idx++] = htole16(0xdc00 | (utfchar & 0x3ff));
		}
	}

	/* Pad with zeros, but no need of explicit NUL termination */
	while (s16idx < s16len)
		s16[s16idx++] = 0;
}

static const struct {
	const char *alias;
	uuid_t uuid;
} uuid_aliases[] = {
	{ "efi",		GPT_ENT_TYPE_EFI },
	{ "bios",		GPT_ENT_TYPE_BIOS_BOOT },
	/* DragonFly */
	{ "ccd",		GPT_ENT_TYPE_DRAGONFLY_CCD },
	{ "label32",		GPT_ENT_TYPE_DRAGONFLY_LABEL32 },
	{ "label64",		GPT_ENT_TYPE_DRAGONFLY_LABEL64 },
	{ "dfly",		GPT_ENT_TYPE_DRAGONFLY_LABEL64 },
	{ "dragonfly",		GPT_ENT_TYPE_DRAGONFLY_LABEL64 },
	{ "hammer",		GPT_ENT_TYPE_DRAGONFLY_HAMMER },
	{ "hammer2",		GPT_ENT_TYPE_DRAGONFLY_HAMMER2 },
	{ "swap",		GPT_ENT_TYPE_DRAGONFLY_SWAP },
	{ "ufs",		GPT_ENT_TYPE_DRAGONFLY_UFS1 },
	{ "vinum",		GPT_ENT_TYPE_DRAGONFLY_VINUM },
	/* FreeBSD */
	{ "freebsd-legacy",	GPT_ENT_TYPE_FREEBSD },
	{ "freebsd-boot",	GPT_ENT_TYPE_FREEBSD_BOOT },
	{ "freebsd-swap",	GPT_ENT_TYPE_FREEBSD_SWAP },
	{ "freebsd-ufs",	GPT_ENT_TYPE_FREEBSD_UFS },
	{ "freebsd-zfs",	GPT_ENT_TYPE_FREEBSD_ZFS },
	/* NetBSD */
	{ "netbsd-ccd",		GPT_ENT_TYPE_NETBSD_CCD },
	{ "netbsd-cgd",		GPT_ENT_TYPE_NETBSD_CGD },
	{ "netbsd-ffs",		GPT_ENT_TYPE_NETBSD_FFS },
	{ "netbsd-lfs",		GPT_ENT_TYPE_NETBSD_LFS },
	{ "netbsd-swap",	GPT_ENT_TYPE_NETBSD_SWAP },
	/* OpenBSD */
	{ "openbsd",		GPT_ENT_TYPE_OPENBSD_DATA },
	/* Apple */
	{ "apple-apfs",		GPT_ENT_TYPE_APPLE_APFS },
	{ "apple-hfs",		GPT_ENT_TYPE_APPLE_HFS },
	{ "apple-ufs",		GPT_ENT_TYPE_APPLE_UFS },
	{ "apple-zfs",		GPT_ENT_TYPE_APPLE_ZFS },
	/* Linux */
	{ "linux",		GPT_ENT_TYPE_LINUX_DATA },
	{ "linux-lvm",		GPT_ENT_TYPE_LINUX_LVM },
	{ "linux-raid",		GPT_ENT_TYPE_LINUX_RAID },
	{ "linux-swap",		GPT_ENT_TYPE_LINUX_SWAP },
	/* Windows */
	{ "windows",		GPT_ENT_TYPE_MS_BASIC_DATA },
	{ "windows-reserved",	GPT_ENT_TYPE_MS_RESERVED },
	{ "windows-recovery",	GPT_ENT_TYPE_MS_RECOVERY },
};

int
parse_uuid(const char *s, uuid_t *uuid)
{
	uint32_t status;
	uuid_t tmp;
	size_t i;

	uuid_from_string(s, uuid, &status);
	if (status == uuid_s_ok)
		return (0);

	for (i = 0; i < NELEM(uuid_aliases); i++) {
		if (strcmp(s, uuid_aliases[i].alias) == 0) {
			*uuid = uuid_aliases[i].uuid;
			return (0);
		}
	}

	uuid_name_lookup(&tmp, s, &status);
	if (status == uuid_s_ok) {
		*uuid = tmp;
		return(0);
	}

	warnx("unknown partition type: %s", s);
	return (EINVAL);
}

void *
gpt_read(int fd, off_t lba, size_t count)
{
	off_t ofs;
	void *buf;

	count *= secsz;
	buf = malloc(count);
	if (buf == NULL)
		return (NULL);

	ofs = lba * secsz;
	if (lseek(fd, ofs, SEEK_SET) != ofs) {
		free(buf);
		return (NULL);
	}

	if (read(fd, buf, count) != (ssize_t)count) {
		if (errno == 0) /* partial read */
			errno = E2BIG;
		free(buf);
		return (NULL);
	}

	return (buf);
}

int
gpt_write(int fd, map_t *map)
{
	off_t ofs;
	size_t count;

	count = map->map_size * secsz;
	ofs = map->map_start * secsz;
	if (lseek(fd, ofs, SEEK_SET) == ofs &&
	    write(fd, map->map_data, count) == (ssize_t)count)
		return (0);

	warnx("%s: failed to write %ju sectors starting at %ju",
	      device_name, (uintmax_t)map->map_size,
	      (uintmax_t)map->map_start);
	return (-1);
}

static int
gpt_mbr(int fd, off_t lba)
{
	struct mbr *mbr;
	map_t *m, *p;
	off_t size, start;
	unsigned int i, pmbr;

	mbr = gpt_read(fd, lba, 1);
	if (mbr == NULL)
		return (-1);

	if (mbr->mbr_sig != htole16(DOSMAGIC)) {
		if (verbose) {
			warnx("%s: MBR not found at sector %ju", device_name,
			      (uintmax_t)lba);
		}
		free(mbr);
		return (0);
	}

	/*
	 * Differentiate between a regular MBR and a PMBR. This is more
	 * convenient in general. A PMBR is one with a single partition
	 * of type 0xee.
	 */
	pmbr = 0;
	for (i = 0; i < 4; i++) {
		if (mbr->mbr_part[i].dp_typ == 0)
			continue;
		if (mbr->mbr_part[i].dp_typ == DOSPTYP_PMBR)
			pmbr++;
		else
			break;
	}
	if (pmbr && (i == 1 || i == 4) && lba == 0) {
		if (pmbr != 1) {
			warnx("%s: Suspicious PMBR at sector %ju",
			      device_name, (uintmax_t)lba);
		} else if (verbose > 1) {
			warnx("%s: PMBR at sector %ju", device_name,
			      (uintmax_t)lba);
		}
		p = map_add(lba, 1LL, MAP_TYPE_PMBR, mbr);
		if (p == NULL)
			return (-1);
		return (0); /* PMBR found, but MBR not found */
	}
	if (pmbr) {
		warnx("%s: Suspicious MBR at sector %ju", device_name,
		      (uintmax_t)lba);
	} else if (verbose > 1) {
		warnx("%s: MBR at sector %ju", device_name, (uintmax_t)lba);
	}

	p = map_add(lba, 1LL, MAP_TYPE_MBR, mbr);
	if (p == NULL)
		return (-1);
	for (i = 0; i < 4; i++) {
		if (mbr->mbr_part[i].dp_typ == 0 ||
		    mbr->mbr_part[i].dp_typ == DOSPTYP_PMBR)
			continue;
		start = le32toh(mbr->mbr_part[i].dp_start);
		size = le32toh(mbr->mbr_part[i].dp_size);
		if (start == 0 && size == 0) {
			warnx("%s: Malformed MBR at sector %ju", device_name,
			      (uintmax_t)lba);
			continue;
		}
		/* start is relative to the offset of the MBR itself. */
		start += lba;
		if (verbose > 2) {
			warnx("%s: MBR part %d: type=%d, start=%ju, size=%ju",
			      device_name, i, mbr->mbr_part[i].dp_typ,
			      (uintmax_t)start, (uintmax_t)size);
		}
		if (mbr->mbr_part[i].dp_typ != DOSPTYP_EXTLBA) {
			m = map_add(start, size, MAP_TYPE_MBR_PART, p);
			if (m == NULL)
				return (-1);
			m->map_index = i;
		} else {
			/* Extended boot record (LBA) */
			if (gpt_mbr(fd, start) == -1)
				return (-1);
		}
	}

	return (1); /* MBR found */
}

static int
gpt_gpt(int fd, off_t lba)
{
	struct gpt_hdr *hdr;
	char *p;
	map_t *m;
	size_t blocks, tblsz;
	uint32_t crc;

	hdr = gpt_read(fd, lba, 1);
	if (hdr == NULL) {
		warn("%s: Reading GPT header at sector %ju failed",
		     device_name, (uintmax_t)lba);
		return (-1);
	}

	if (memcmp(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig)))
		goto fail_hdr;

	crc = le32toh(hdr->hdr_crc_self);
	hdr->hdr_crc_self = 0;
	if (crc32(hdr, le32toh(hdr->hdr_size)) != crc) {
		if (verbose) {
			warnx("%s: Bad CRC in GPT header at sector %ju",
			      device_name, (uintmax_t)lba);
		}
		goto fail_hdr;
	}

	tblsz = le32toh(hdr->hdr_entries) * le32toh(hdr->hdr_entsz);
	blocks = tblsz / secsz + ((tblsz % secsz) ? 1 : 0);

	p = gpt_read(fd, le64toh(hdr->hdr_lba_table), blocks);
	if (p == NULL) {
		warn("%s: Reading GPT table at sector %ju failed",
		     device_name, (uintmax_t)le64toh(hdr->hdr_lba_table));
		return (-1);
	}

	if (crc32(p, tblsz) != le32toh(hdr->hdr_crc_table)) {
		if (verbose) {
			warnx("%s: Bad CRC in GPT table at sector %ju",
			      device_name,
			      (uintmax_t)le64toh(hdr->hdr_lba_table));
		}
		goto fail_ent;
	}

	if (verbose > 1) {
		warnx("%s: %s GPT at sector %ju", device_name,
		      (lba == 1) ? "Primary" : "Secondary", (uintmax_t)lba);
	}

	m = map_add(lba, 1,
	    (lba == 1) ? MAP_TYPE_PRI_GPT_HDR : MAP_TYPE_SEC_GPT_HDR, hdr);
	if (m == NULL)
		return (-1);

	m = map_add(le64toh(hdr->hdr_lba_table), blocks,
	    (lba == 1) ? MAP_TYPE_PRI_GPT_TBL : MAP_TYPE_SEC_GPT_TBL, p);
	if (m == NULL)
		return (-1);

	return (1); /* found */

 fail_ent:
	free(p);

 fail_hdr:
	free(hdr);
	return (0); /* not found */
}

static int
gpt_read_table(bool primary)
{
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	map_t *map, *tbl;
	uuid_t type;
	off_t size;
	uint32_t i;
	char *s;

	if (primary) {
		map = map_find(MAP_TYPE_PRI_GPT_HDR);
		hdr = map->map_data;
		tbl = map_find(MAP_TYPE_PRI_GPT_TBL);
	} else {
		map = map_find(MAP_TYPE_SEC_GPT_HDR);
		hdr = map->map_data;
		tbl = map_find(MAP_TYPE_SEC_GPT_TBL);
	}

	for (i = 0; i < le32toh(hdr->hdr_entries); i++) {
		/*
		 * Use generic pointer to deal with
		 * hdr->hdr_entsz != sizeof(*ent).
		 */
		ent = (void *)((char *)tbl->map_data + i *
		    le32toh(hdr->hdr_entsz));
		if (uuid_is_nil(&ent->ent_type, NULL))
			continue;

		size = le64toh(ent->ent_lba_end) -
		    le64toh(ent->ent_lba_start) + 1LL;
		if (verbose > 2) {
			uuid_dec_le(&ent->ent_type, &type);
			uuid_to_string(&type, &s, NULL);
			warnx("%s: GPT partition %d: type=%s, start=%ju, "
			      "size=%ju",
			      device_name, i, s,
			      (uintmax_t)le64toh(ent->ent_lba_start),
			      (uintmax_t)size);
			free(s);
		}

		map = map_add(le64toh(ent->ent_lba_start), size,
		    MAP_TYPE_GPT_PART, ent);
		if (map == NULL)
			return (-1);

		map->map_index = i;
	}

	return (0);
}

int
gpt_open(const char *dev)
{
	struct stat sb;
	static char device_path[MAXPATHLEN];
	int fd, mode, found;

	mode = readonly ? O_RDONLY : O_RDWR|O_EXCL;

	strlcpy(device_path, dev, sizeof(device_path));
	device_name = device_path;

	if ((fd = open(device_path, mode)) != -1)
		goto opened;

	snprintf(device_path, sizeof(device_path), "%s%s", _PATH_DEV, dev);
	device_name = device_path + strlen(_PATH_DEV);
	errno = 0;
	if ((fd = open(device_path, mode)) != -1)
		goto opened;

	return (-1);

 opened:
	if (fstat(fd, &sb) == -1)
		goto close;

	if ((sb.st_mode & S_IFMT) != S_IFREG) {
		struct partinfo partinfo;

		if (ioctl(fd, DIOCGPART, &partinfo) < 0)
			goto close;
		secsz = partinfo.media_blksize;
		mediasz = partinfo.media_size;
	} else {
		secsz = 512;	/* Fixed size for files. */
		if (sb.st_size % secsz) {
			errno = EINVAL;
			goto close;
		}
		mediasz = sb.st_size;
	}

	/*
	 * We require an absolute minimum of 6 sectors. One for the MBR,
	 * 2 for the GPT header, 2 for the GPT table and one to hold some
	 * user data. Let's catch this extreme border case here so that
	 * we don't have to worry about it later.
	 */
	if (mediasz / secsz < 6) {
		errno = ENODEV;
		goto close;
	}

	if (verbose) {
		warnx("%s: mediasize=%ju; sectorsize=%u; blocks=%ju",
		      device_name, (uintmax_t)mediasz, secsz,
		      (uintmax_t)(mediasz / secsz));
	}

	map_init(mediasz / secsz);

	if ((found = gpt_mbr(fd, 0LL)) == -1)
		goto close;

	if (!found) {
		/*
		 * MBR not found; try GPT.
		 */
		if ((found = gpt_gpt(fd, 1LL)) == -1)
			goto close;

		if (found) {
			struct gpt_hdr *hdr;
			map_t *map;
			off_t lba;

			if (gpt_read_table(true) < 0)
				goto close;

			/*
			 * Read secondary GPT from position stored in the
			 * primary header.
			 */
			map = map_find(MAP_TYPE_PRI_GPT_HDR);
			hdr = map->map_data;
			lba = le64toh(hdr->hdr_lba_alt);
			if (lba < mediasz / secsz) {
				if (gpt_gpt(fd, lba) == -1)
					goto close;
			}
		} else {
			/*
			 * Try read secondary GPT at the end of the disk.
			 */
			found = gpt_gpt(fd, mediasz / secsz - 1LL);
			if (found == -1) {
				goto close;
			} else if (found) {
				if (gpt_read_table(false) < 0)
					goto close;
			}
		}
	}

	return (fd);

 close:
	close(fd);
	return (-1);
}

void
gpt_close(int fd)
{
	/* XXX post processing? */
	close(fd);
}

#ifndef _LIBEFIVAR
static struct {
	int (*fptr)(int, char *[]);
	const char *name;
} cmdsw[] = {
	{ cmd_add,	"add" },
	{ cmd_boot,	"boot" },
	{ cmd_create,	"create" },
	{ cmd_destroy,	"destroy" },
	{ cmd_expand,	"expand" },
	{ cmd_init,	"init" },
	{ cmd_label,	"label" },
	{ cmd_migrate,	"migrate" },
	{ cmd_recover,	"recover" },
	{ cmd_remove,	"remove" },
	{ cmd_show,	"show" },
	{ NULL,		NULL },
};

static void
usage(void)
{
	const char *prgname = getprogname();

	fprintf(stderr,
		"usage: %s [-rv] <command> [options] <device> ...\n"
		"       %s show <device>\n",
		prgname, prgname);
	exit(1);
}

static void
prefix(const char *cmd)
{
	char *pfx;
	const char *prg;

	prg = getprogname();
	pfx = malloc(strlen(prg) + strlen(cmd) + 2);
	/* Don't bother failing. It's not important */
	if (pfx == NULL)
		return;

	sprintf(pfx, "%s %s", prg, cmd);
	setprogname(pfx);
}

int
main(int argc, char *argv[])
{
	char *cmd;
	int ch, i;

	/* Get the generic options */
	while ((ch = getopt(argc, argv, "h:rv")) != -1) {
		switch(ch) {
		case 'r':
			readonly = true;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (argc == optind)
		usage();

	cmd = argv[optind++];
	for (i = 0; cmdsw[i].name != NULL && strcmp(cmd, cmdsw[i].name); i++)
		;
	if (cmdsw[i].fptr == NULL)
		errx(1, "unknown command: %s", cmd);

	prefix(cmd);
	return ((*cmdsw[i].fptr)(argc, argv));
}
#endif /* !_LIBEFIVAR */
