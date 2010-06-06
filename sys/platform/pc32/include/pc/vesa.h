/*-
 * Copyright (c) 1998 Michael Smith and Kazutaka YOKOTA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/pc/vesa.h,v 1.7 1999/12/29 04:33:12 peter Exp $
 */

#ifndef _MACHINE_PC_VESA_H
#define _MACHINE_PC_VESA_H

struct vesa_info {
	/* mandatory fields */
	uint8_t		v_sig[4];	/* VESA */
	uint16_t	v_version;	/* ver in BCD */
	uint32_t	v_oemstr;	/* OEM string */
	uint32_t	v_flags;	/* flags */
#define V_DAC8		(1<<0)
#define V_NONVGA	(1<<1)
#define V_SNOW		(1<<2)
	uint32_t	v_modetable;	/* modes */
	uint16_t	v_memsize;	/* in 64K */
	/* 2.0 */
	uint16_t	v_revision;	/* software rev */
	uint32_t	v_vendorstr;	/* vendor */
	uint32_t	v_prodstr;	/* product name */
	uint32_t	v_revstr;	/* product rev */
} __packed;

struct vesa_mode  {
	/* mandatory fields */
	uint16_t	v_modeattr;
#define V_MODESUPP	(1<<0)	/* VESA mode attributes */
#define V_MODEBIOSOUT	(1<<2)
#define V_MODECOLOR	(1<<3)
#define V_MODEGRAPHICS	(1<<4)
#define V_MODENONVGA	(1<<5)
#define V_MODENONBANK	(1<<6)
#define V_MODELFB	(1<<7)
#define V_MODEVESA	(1<<16)	/* Private attributes */
	uint8_t		v_waattr;
	uint8_t		v_wbattr;
#define V_WATTREXIST	(1<<0)
#define V_WATTRREAD	(1<<1)
#define V_WATTRWRITE	(1<<2)
	uint16_t	v_wgran;
	uint16_t	v_wsize;
	uint16_t	v_waseg;
	uint16_t	v_wbseg;
	uint32_t	v_posfunc;
	uint16_t	v_bpscanline;
	/* fields optional for 1.0/1.1 implementations */
	uint16_t	v_width;
	uint16_t	v_height;
	uint8_t		v_cwidth;
	uint8_t		v_cheight;
	uint8_t		v_planes;
	uint8_t		v_bpp;
	uint8_t		v_banks;
	uint8_t		v_memmodel;
#define V_MMTEXT	0
#define V_MMCGA		1
#define V_MMHGC		2
#define V_MMEGA		3
#define V_MMPACKED	4
#define V_MMSEQU256	5
#define V_MMDIRCOLOR	6
#define V_MMYUV		7
	uint8_t		v_banksize;
	uint8_t		v_ipages;
	uint8_t		v_reserved0;
	/* fields for 1.2+ implementations */
	uint8_t		v_redmasksize;
	uint8_t		v_redfieldpos;
	uint8_t		v_greenmasksize;
	uint8_t		v_greenfieldpos;
	uint8_t		v_bluemasksize;
	uint8_t		v_bluefieldpos;
	uint8_t		v_resmasksize;
	uint8_t		v_resfieldpos;
	uint8_t		v_dircolormode;
	/* 2.0 implementations */
	uint32_t	v_lfb;
	uint32_t	v_offscreen;
	uint16_t	v_offscreensize;
};

#ifdef _KERNEL

#define VESA_MODE(x)	((x) >= M_VESA_BASE)

#endif

#endif /* !_MACHINE_PC_VESA_H */
