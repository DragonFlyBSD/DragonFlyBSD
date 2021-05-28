/*	$NetBSD: elf.c,v 1.1 2018/09/12 00:00:00 maxv Exp $	*/

/*
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <nvmm.h>

#include "common.h"

#define	ELFSIZE	64
#include <sys/exec_elf.h>

#define ELF_MAXPHNUM	128
#define ELF_MAXSHNUM	32768

static int
elf_check_header(Elf_Ehdr *eh)
{
	if (eh->e_shnum > ELF_MAXSHNUM || eh->e_phnum > ELF_MAXPHNUM)
		return -1;
	return 0;
}

static uint64_t
elf_parse(struct nvmm_machine *mach, char *base)
{
	Elf_Ehdr *ehdr = (Elf_Ehdr *)base;
	Elf_Phdr *phdr;
	uintptr_t hva;
	gpaddr_t lastgpa;
	size_t i;

	if (elf_check_header(ehdr) == -1)
		errx(EXIT_FAILURE, "wrong ELF header");
	phdr = (Elf_Phdr *)((char *)ehdr + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		hva = toyvirt_mem_add(mach, phdr[i].p_vaddr,
		    roundup(phdr[i].p_filesz, PAGE_SIZE));
		memcpy((void *)hva, base + phdr[i].p_offset, phdr[i].p_filesz);

		lastgpa = phdr[i].p_vaddr +
		    roundup(phdr[i].p_filesz, PAGE_SIZE);
	}

	/* Add 128 pages after the kernel image. */
	toyvirt_mem_add(mach, lastgpa, 128 * PAGE_SIZE);

	return ehdr->e_entry;
}

int
elf_map(struct nvmm_machine *mach, const char *path, uint64_t *rip)
{
	struct stat st;
	char *base = NULL;
	int fd, ret = -1;

	fd = open(path, O_RDONLY);
	if (fstat(fd, &st) == -1)
		goto out;
	if ((size_t)st.st_size < sizeof(Elf_Ehdr))
		goto out;
	base = malloc(st.st_size);
	pread(fd, base, st.st_size, 0);

	*rip = elf_parse(mach, base);
	ret = 0;

out:
	close(fd);
	free(base);
	return ret;
}
