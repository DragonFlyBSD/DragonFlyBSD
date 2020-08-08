/*-
 * Copyright (c) 2003 Kip Macy All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_CKPT_H_
#define _SYS_CKPT_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_MOUNT_H_
#include <sys/mount.h>
#endif
#ifndef _SYS_SIGNALVAR_H_
#include <sys/signalvar.h>
#endif

#define CKPT_MAXTHREADS	256

struct ckpt_filehdr {
	int		cfh_magic;	/* XXX implement */
	int		cfh_nfiles;
	int		cfh_reserved[8];
};

struct ckpt_vminfo {
	segsz_t		cvm_dsize;	/* in pages */
	segsz_t		cvm_tsize;	/* in pages */
	segsz_t		cvm_reserved1[4];
	caddr_t		cvm_daddr;
	caddr_t		cvm_taddr;
	caddr_t		cvm_reserved2[4];
};

struct ckpt_fileinfo {
	int		cfi_index;
	u_int		cfi_flags;	/* saved f_flag	*/
	off_t		cfi_offset;	/* saved f_offset */
	fhandle_t	cfi_fh;
	int		cfi_type;
	int		cfi_ckflags;
	int		cfi_reserved[6];
};

#define CKFIF_ISCKPTFD	0x0001

struct ckpt_siginfo {
	int		csi_ckptpisz;
	struct sigacts	csi_sigacts;
	struct itimerval csi_itimerval;
	int		csi_sigparent;
	sigset_t	csi_sigmask;
	int		csi_reserved[6];
};

/*
 * Elf_Phdr is based on the inclusion of elf32 or elf64.  If neither was
 * included, we don't know what to do with it.  If the source needs the
 * structure, generate a compile-time error.
 */
#ifdef __ELF_WORD_SIZE

struct vn_hdr {
	fhandle_t	vnh_fh;
	Elf_Phdr	vnh_phdr;
	int		vnh_reserved[8];
};

#endif

#ifdef _KERNEL
#ifdef DEBUG
#define	TRACE_ENTER kprintf("entering %s at %s:%d\n", __func__, __FILE__, __LINE__)
#define	TRACE_EXIT kprintf("exiting %s at %s:%d\n", __func__, __FILE__, __LINE__)
#define	TRACE_ERR kprintf("failure encountered in %s at %s:%d\n", __func__, __FILE__, __LINE__)
#define PRINTF(args) kprintf args
#else
#define TRACE_ENTER
#define TRACE_EXIT
#define TRACE_ERR
#define PRINTF(args)
#endif	/* DEBUG */
#endif	/* _KERNEL */

#endif	/* _SYS_CKPT_H_ */
