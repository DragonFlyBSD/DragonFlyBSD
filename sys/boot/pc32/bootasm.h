/* 
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sys/boot/pc32/bootasm.h,v 1.1 2004/07/18 23:39:55 dillon Exp $
 */

#define NHRDRV		0x475
#define BOOT0_ORIGIN	0x600		/* boot0 relocated	*/
#define FAKE		0x800		/* Fake partition entry */
#define LOAD		0x7c00		/* Load address		*/

#define MEM_ARG_SIZE	0x18
#define MEM_PAGE_SIZE	0x1000

#define MEM_REL		0x700		/* Relocation address	*/
#define MEM_ARG		0x900		/* Arguments		*/
#define MEM_ORG		0x7c00		/* Origin		*/
#define MEM_BUF		0x8c00		/* Load area		*/
#define MEM_BTX		0x9000		/* BTX start		*/
#define MEM_JMP		0x9010		/* BTX entry point	*/
#define MEM_USR		0xa000		/* Client start		*/
#define BDA_BOOT	0x472		/* Boot howto flag	*/
#define BDA_MEM		0x413		/* Free memory		*/
#define BDA_KEYFLAGS	0x417		/* Keyboard shift-state flags	*/
#define BDA_SCR		0x449		/* Video mode		*/
#define BDA_POS		0x450		/* Cursor position	*/
#define BDA_KEYBOARD	0x496		/* BDA byte with keyboard bit */

#define MEM_BTX_START	0x1000		/* start of BTX memory */
#define MEM_BTX_ESP0	0x1800		/* Supervisor stack */
#define MEM_BTX_BUF	0x1800		/* Scratch buffer stack */
#define MEM_BTX_ESP1	0x1e00		/* Link stack */
#define MEM_BTX_IDT	0x1e00		/* IDT */
#define MEM_BTX_TSS	0x1f98		/* TSS */
#define MEM_BTX_MAP	0x2000		/* I/O bit map */
#define MEM_BTX_DIR	0x4000		/* Page directory */
#define MEM_BTX_TBL	0x5000		/* Page tables */
#define MEM_BTX_ORG	0x9000		/* base of BTX code */
#define MEM_BTX_ENTRY	0x9010		/* BTX starts execution here */
#define MEM_BTX_USR	0xa000		/* base of BTX client/user memory */
#define MEM_BTX_USR_ARG	0xa100
#define MEM_BTX_LDR_OFF	MEM_PAGE_SIZE	/* offset of btx in the loader

/*
 * CDBOOT
 */
#define MEM_BTX_LDR_OFFSET	0x1000	/* Offset of BTX in the loader */

