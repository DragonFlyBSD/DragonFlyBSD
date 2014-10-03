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
 * $DragonFly: src/sys/boot/pc32/bootasm.h,v 1.4 2004/07/27 19:37:15 dillon Exp $
 */

/* 
 * Set the bootloader address set.
 *
 * UNSET - default backwards compatible boot blocks
 *   1   - experimental move addresses above 0x1000 and hardwire the user
 *	   stack.
 *   2   - experimental move addresses abobe 0x2000 and hardwire the user
 *	   stack.
 *      NOTE: some changes to the standard bootloader address set and the
 *            rest of the code are not reflected in the experimental sets
 */
/* #define BOOT_NEWBOOTLOADER 2 */

/*
 * Various fixed constants that do not change
 */

#define BDA_MEM		0x413		/* Free memory		*/
#define BDA_SCR		0x449		/* Video mode		*/
#define BDA_POS		0x450		/* Cursor position	*/
#define BDA_BOOT	0x472		/* Boot howto flag	*/
#define BDA_NHRDRV	0x475
#define BDA_KEYBOARD	0x496		/* BDA byte with keyboard bit */

/*
 * Structural equivalences
 */
#define BOOTINFO_SIZE	0x48		/* bootinfo structure size */
#define MEM_ARG_SIZE	0x18
#define MEM_PAGE_SIZE	0x1000
#define MEM_BTX_LDR_OFF	MEM_PAGE_SIZE	/* offset of btx in the loader */
#define USR_ARGOFFSET	(BOOTINFO_SIZE+MEM_ARG_SIZE)

/* -------- WARNING, BOOT0 STACK BELOW MEM_BIOS_LADDR -------- */
#define MEM_BIOS_LADDR	0x7c00		/* Load address (static/BIOS) */

/*
 * This is the origin of boot2.bin relative to the BTX user address space
 * (e.g. physical address would be MEM_BTX_USR+BOOT2_VORIGIN).
 *
 * The physical origin is typically around 0xC000 and limits the size of
 * boot2 to 16K, otherwise the loader will overflow the segment in v86 mode.
 */
#define BOOT2_VORIGIN	0x2000

/*
 * NOTE: BOOT0_ORIGIN is extracted from this file and used in boot0/Makefile
 * 	 BOOT1_ORIGIN is extracted from this file and used in boot2/Makefile
 *
 *	 NOTE: boot0 has a variable space after its sector which contains
 *	 the fake partition and other variables.  ~128 bytes should be reserved
 *	 for this variable space, but it may overlap BOOT1's data space.
 */

#if !defined(BOOT_NEWBOOTLOADER)

/************************************************************************
 *			STANDARD BOOTLOADER ADDRESS SET 		*
 ************************************************************************
 *
 *
 */

#define USR_ARGSPACE	0x1000		/* BTX loader / ttl argspace reserved */

#define BOOT0_ORIGIN	0x600		/* boot0 relocated to (512+128 bytes) */
/* -------- WARNING, BOOT1 STACK BELOW BOOT1_ORIGIN ------- */
#define BOOT1_ORIGIN	0x700		/* boot1 relocated to (512 bytes) */
#define MEM_ARG		0x900		/* tmp arg store cdboot/pxeboot */

#define MEM_BTX_ESP	0x1000		/* btxldr top of stack? */
#define MEM_BTX_START	0x1000		/* start of BTX memory */
#define MEM_BTX_ESP0	0x1800		/* Supervisor stack */
#define MEM_BTX_BUF	0x1800		/* Scratch buffer stack */
#define MEM_BTX_ESPR	0x5e00		/* Real mode stack */
#define MEM_BTX_IDT	0x5e00		/* IDT */
#define MEM_BTX_TSS	0x5f98		/* TSS */
#define MEM_BTX_MAP	0x6000		/* I/O bit map */
#define MEM_BTX_TSS_END	0x7fff		/* Start of user memory */

/*
 * NOTE: page table location is hardwired in /usr/src/usr.sbin/btxld/btx.h
 */
#define MEM_BTX_ZEND	0x7000		/* Zero from IDT to here in btx.S */

/********************   0x7c00 BIOS LOAD ADDRESS (512 bytes) **********/

/*
 * NOTE: BOOT2_LOAD_BUF also determines where the btx loader and boot2.bin
 *       code are loaded, since they are all in the boot2 file.
 */
#define BOOT2_LOAD_BUF	0x8c00		/* boot1 loads boot2	*/
#define MEM_BTX_ORG	0x9000		/* base of BTX code */
#define MEM_BTX_ENTRY	0x9010		/* BTX starts execution here */
/*
 * WARNING!  The USR area may be messed around with in 16 bit code mode,
 *           data loaded should probably not cross 0xffff (e.g. boot2 loads
 *	     ~8K at MEM_BTX_USR).
 *
 *	     MEM_BTX_USR is basically the segment offset BTX uses when
 *	     running 'client' code.  So address 0 in the client code will
 *	     actually be physical address MEM_BTX_USR.
 */
#define MEM_BTX_USR	0xa000		/* base of BTX client/user memory */
#define MEM_BTX_USR_ARG	0xa100

/*
 * By default the user stack is (theoretically) placed at the top of
 * BIOS memory (typically around the 640K mark).  See btx.S.  BTX loads
 * the stack from a BIOS memory address (BDA_MEM) rather than figuring it
 * out from the smap.
 *
 * There aren't really any other places we can put it short of intruding on
 * the kernel/module load space.
 */
/*#define MEM_BTX_USR_STK	0x3000000*/
/*#define MEM_BTX_USR_STK	0x0F0000*/

#elif defined(BOOT_NEWBOOTLOADER) && BOOT_NEWBOOTLOADER == 1

/************************************************************************
 *		EXPERIMENTAL BOOTLOADER ADDRESS SET 1			*
 ************************************************************************
 *
 *
 */

#define USR_ARGSPACE	0x1000		/* BTX loader / ttl argspace reserved */
#define MEM_BTX_USR_STK	0x90000		/* (phys addr) btx client usr stack */

#define MEM_BTX_START	0x1000		/* (unchanged)		*/
#define MEM_BTX_ESP0	0x1800		/* (unchanged)		*/
#define MEM_BTX_BUF	0x1800		/* (unchanged)		*/
#define MEM_BTX_ESP1	0x1e00		/* (unchanged)		*/
#define MEM_BTX_IDT	0x1e00		/* (unchanged)		*/
#define MEM_BTX_TSS	0x1f98		/* (unchanged)		*/
#define MEM_BTX_MAP	0x2000		/* (unchanged)		*/
#define MEM_BTX_DIR	0x4000		/* (unchanged)		*/
#define MEM_BTX_TBL	0x5000		/* (unchanged)		*/
#define MEM_BTX_ZEND	0x7000		/* (unchanged)		*/

#define MEM_BTX_ESP	0x7800		/* don't use 0x1000 		*/
#define BOOT0_ORIGIN	0x7800		/* boot0 relocated		*/
#define BOOT1_ORIGIN	0x7900		/* boot1 relocated (data only?) */
#define MEM_ARG		0x7b00		/* cdboot/pxeboot disk/slice xfer */
/********************   0x7c00 BIOS LOAD ADDRESS (512 bytes) **********/

#define BOOT2_LOAD_BUF	0x8c00		/* (unchanged)		*/
#define MEM_BTX_ORG	0x9000		/* (unchanged)		*/
#define MEM_BTX_ENTRY	0x9010		/* (unchanged)		*/

#define MEM_BTX_USR	0xa000		/* (unchanged)		*/
#define MEM_BTX_USR_ARG	0xa100		/* (unchanged)		*/


#elif defined(BOOT_NEWBOOTLOADER) && BOOT_NEWBOOTLOADER == 2

/************************************************************************
 *		EXPERIMENTAL BOOTLOADER ADDRESS SET 2			*
 ************************************************************************
 *
 *
 */
#define USR_ARGSPACE	0x1000		/* BTX loader / ttl argspace reserved */
#define MEM_BTX_USR_STK	0x90000		/* (phys addr) btx client usr stack */

#define MEM_BTX_START	0x2000		/* (unchanged)		*/
#define MEM_BTX_ESP0	0x2800		/* (unchanged)		*/
#define MEM_BTX_BUF	0x2800		/* (unchanged)		*/
#define MEM_BTX_ESP1	0x2e00		/* (unchanged)		*/
#define MEM_BTX_IDT	0x2e00		/* (unchanged)		*/
#define MEM_BTX_TSS	0x2f98		/* (unchanged)		*/
#define MEM_BTX_MAP	0x3000		/* (unchanged)		*/
#define MEM_BTX_DIR	0x5000		/* (unchanged)		*/
/****** MEM_BTX_TBL (16K) SUPPORT REMOVED ***********************/
#define MEM_BTX_ZEND	0x6000		/* (unchanged)		*/

#define MEM_BTX_ESP	0x7800		/* don't use 0x1000 		*/
#define BOOT0_ORIGIN	0x7800		/* boot0 relocated		*/
#define BOOT1_ORIGIN	0x7900		/* boot1 relocated (data only?) */
#define MEM_ARG		0x7b00		/* cdboot/pxeboot disk/slice xfer */
/********************   0x7c00 BIOS LOAD ADDRESS (512 bytes) **********/

#define BOOT2_LOAD_BUF	0x8c00		/* (unchanged)		*/
#define MEM_BTX_ORG	0x9000		/* (unchanged)		*/
#define MEM_BTX_ENTRY	0x9010		/* (unchanged)		*/

#define MEM_BTX_USR	0xa000		/* base of BTX client/user memory */
#define MEM_BTX_USR_ARG	0xa100		/* boot1->boot2 disk/slice xfer */

#else

#error "BAD BOOT_NEWBOOTLOADER SETTING.  UNSET TO GET DEFAULT"

#endif	/* BOOT_NEWBOOTLOADER */
