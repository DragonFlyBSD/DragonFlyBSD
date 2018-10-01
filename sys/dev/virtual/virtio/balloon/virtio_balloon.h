/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/dev/virtio/balloon/virtio_balloon.h 326022 2017-11-20 19:36:21Z pfg $
 */
/*
 * Copyright (c) 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Diederik de Groot <info@talon.nl>
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

/* Driver for VirtIO memory balloon devices. */

#ifndef _VIRTIO_BALLOON_H
#define _VIRTIO_BALLOON_H

/* Feature bits. */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST	0x1 /* Tell before reclaiming pages */
#define VIRTIO_BALLOON_F_STATS_VQ	0x2 /* Memory stats virtqueue */
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM 0x3 /* Deflate on Out Of Memory */

/* Size of a PFN in the balloon interface. */
#define VIRTIO_BALLOON_PFN_SHIFT 12

struct virtio_balloon_config {
	/* Number of pages host wants Guest to give up. */
	uint32_t num_pages;

	/* Number of pages we've actually got in balloon. */
	uint32_t actual;
};

#define VTBALLOON_S_SWAP_IN	 0	/* The amount of memory that has been swapped in (in bytes) */
#define VTBALLOON_S_SWAP_OUT	 1	/* The amount of memory that has been swapped out to disk (in bytes). */
#define VTBALLOON_S_MAJFLT	 2	/* The number of major page faults that have occurred. */
#define VTBALLOON_S_MINFLT	 3	/* The number of minor page faults that have occurred. */
#define VTBALLOON_S_MEMFREE	 4	/* The amount of memory not being used for any purpose (in bytes). */
#define VTBALLOON_S_MEMTOT	 5	/* The total amount of memory available (in bytes). */
#define VTBALLOON_S_AVAIL	 6	/* The amount of availabe memory (in bytes) as in linux-proc */
#define VTBALLOON_S_CACHES	 7	/* Disk-File caches */
#define VTBALLOON_S_HTLB_PGALLOC 8	/* Hugetlb page allocations */
#define VTBALLOON_S_HTLB_PGFAIL	 9	/* Hugetlb page allocation failures */
#define VTBALLOON_S_NR		10

#define VTBALLOON_S_NAMES_WITH_PREFIX(VTBALLOON_S_NAMES_prefix) { \
	VTBALLOON_S_NAMES_prefix "swap-in", \
	VTBALLOON_S_NAMES_prefix "swap-out", \
	VTBALLOON_S_NAMES_prefix "major-faults", \
	VTBALLOON_S_NAMES_prefix "minor-faults", \
	VTBALLOON_S_NAMES_prefix "free-memory", \
	VTBALLOON_S_NAMES_prefix "total-memory", \
	VTBALLOON_S_NAMES_prefix "available-memory", \
	VTBALLOON_S_NAMES_prefix "disk-file-caches", \
	VTBALLOON_S_NAMES_prefix "hugetlb-allocations", \
	VTBALLOON_S_NAMES_prefix "hugetlb-failures" \
}
#define VTBALLOON_S_NAMES VTBALLOON_S_NAMES_WITH_PREFIX("")

/*
 * Memory statistics structure.
 * Driver fills an array of these structures and passes to device.
 *
 * NOTE: fields are laid out in a way that would make compiler add padding
 * between and after fields, but the virtio specification does not allow
 * for this. So the struct has to be packed.
 */
struct vtballoon_stat {
	uint16_t tag;
	uint64_t val;
} __packed;

#endif /* _VIRTIO_BALLOON_H */
