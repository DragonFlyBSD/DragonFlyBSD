/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Johannes Hofmann <Hoannes.Hofmann.gmx.de> and
 * Joerg Sonnenberger <joerg@bec.de.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $DragonFly: src/sys/sys/firmware.h,v 1.2 2006/05/20 02:42:13 dillon Exp $
 */

#ifndef _SYS_FIRMWARE_H_
#define _SYS_FIRMWARE_H_

#ifndef _KERNEL
#error "No user-servicable parts inside"
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <machine/bus.h>
#include <machine/bus_dma.h>

struct fw_image {
	TAILQ_ENTRY(fw_image) fw_link;
	int		 fw_refcnt;
	const char	*fw_name;
	size_t		 fw_imglen;
	c_caddr_t	*fw_image;
	bus_dma_tag_t	 fw_dma_tag;
	bus_dmamap_t	 fw_dma_map;
	bus_addr_t	 fw_dma_addr;
}; 

struct fw_image	*firmware_image_load(const char *);
void		firmware_image_unload(struct fw_image *);

#endif
