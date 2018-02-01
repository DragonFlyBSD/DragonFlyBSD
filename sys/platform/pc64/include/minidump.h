/*-
 * Copyright (c) 2006 Peter Wemm
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_MINIDUMP_H_
#define	_MACHINE_MINIDUMP_H_ 1

#define	MINIDUMP1_MAGIC		"minidump FreeBSD/amd64"
#define	MINIDUMP1_VERSION	1

struct minidumphdr1 {
	char magic[24];
	uint32_t version;
	uint32_t msgbufsize;
	uint32_t bitmapsize;
	uint32_t ptesize;	/* 2-level page table */
	uint64_t kernbase;
	uint64_t dmapbase;
	uint64_t dmapend;
};

#define	MINIDUMP2_MAGIC		"minidump DFlyBSD/x86-64"
#define	MINIDUMP2_VERSION	2

struct minidumphdr2 {
	char magic[24];
	uint32_t version;
	uint32_t unused01;

	uint64_t msgbufsize;
	uint64_t bitmapsize;
	uint64_t ptesize;	/* 4-level page table */
	uint64_t kernbase;
	uint64_t dmapbase;
	uint64_t dmapend;
};

#endif /* _MACHINE_MINIDUMP_H_ */
