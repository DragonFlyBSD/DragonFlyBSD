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
 * $FreeBSD: src/sbin/gpt/gpt.h,v 1.11 2006/06/22 22:05:28 marcel Exp $
 */

#ifndef _GPT_H_
#define	_GPT_H_

#include <sys/cdefs.h>
#include <sys/diskmbr.h>
#include <sys/endian.h>
#include <sys/gpt.h>

#include <uuid.h>

#include "map.h"

struct __packed mbr {
	uint16_t		mbr_code[223];
	struct dos_partition	mbr_part[4];
	uint16_t		mbr_sig;
};
CTASSERT(sizeof(struct mbr) == 512);

extern char *device_name;
extern off_t mediasz;
extern u_int secsz;
extern int readonly, verbose;

uint32_t crc32(const void *, size_t);
void	gpt_close(int);
int	gpt_open(const char *);
void*	gpt_read(int, off_t, size_t);
int	gpt_write(int, map_t *);
int	parse_uuid(const char *, uuid_t *);

uint8_t *utf16_to_utf8(uint16_t *);
void	utf8_to_utf16(const uint8_t *, uint16_t *, size_t);

int	cmd_add(int, char *[]);
int	cmd_boot(int, char *[]);
int	cmd_create(int, char *[]);
int	cmd_destroy(int, char *[]);
int	cmd_expand(int, char *[]);
int	cmd_label(int, char *[]);
int	cmd_init(int, char *[]);
int	cmd_migrate(int, char *[]);
int	cmd_recover(int, char *[]);
int	cmd_remove(int, char *[]);
int	cmd_show(int, char *[]);

void	add_defaults(int fd);
void	do_destroy(int fd);

#endif /* _GPT_H_ */
