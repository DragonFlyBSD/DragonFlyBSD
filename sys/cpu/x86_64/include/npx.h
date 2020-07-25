/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)npx.h	5.3 (Berkeley) 1/18/91
 * $FreeBSD: src/sys/i386/include/npx.h,v 1.18.2.1 2001/08/15 01:23:52 peter Exp $
 */

/*
 * 287/387 NPX Coprocessor Data Structures and Constants
 * W. Jolitz 1/90
 */

#ifndef _CPU_NPX_H_
#define	_CPU_NPX_H_

#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
#endif

/* Environment information of floating point unit */
struct	env87 {
	int32_t		en_cw;		/* control word (16bits) */
	int32_t		en_sw;		/* status word (16bits) */
	int32_t		en_tw;		/* tag word (16bits) */
	int32_t		en_fip;		/* floating point instruction pointer */
	uint16_t	en_fcs;		/* floating code segment selector */
	uint16_t	en_opcode;	/* opcode last executed (11 bits ) */
	int32_t		en_foo;		/* floating operand offset */
	int32_t		en_fos;		/* floating operand segment selector */
};

/* Contents of each x87 floating point accumulator */
struct	fpacc87 {
	uint8_t		fp_bytes[10];
};

/* Floating point context (i386 fnsave/frstor) */
struct	save87 {
	struct	env87 sv_env;	/* floating point control/status */
	struct	fpacc87	sv_ac[8];	/* accumulator contents, 0-7 */
	uint8_t		sv_pad0[4];	/* saved status word (now unused) */
	/*
	 * Bogus padding for emulators.  Emulators should use their own
	 * struct and arrange to store into this struct (ending here)
	 * before it is inspected for ptracing or for core dumps.  Some
	 * emulators overwrite the whole struct.  We have no good way of
	 * knowing how much padding to leave.  Leave just enough for the
	 * GPL emulator's i387_union (176 bytes total).
	 */
	uint8_t		sv_pad[64];
};

struct envxmm {
	uint16_t	en_cw;		/* control word (16bits) */
	uint16_t	en_sw;		/* status word (16bits) */
	uint16_t	en_tw;		/* tag word (16bits) */
	uint16_t	en_opcode;	/* opcode last executed (11 bits) */
	uint32_t	en_fip;		/* fp instruction pointer */
	uint16_t	en_fcs;		/* fp code segment selector */
	uint16_t	en_pad0;	/* padding */
	uint32_t	en_foo;		/* fp operand offset */
	uint16_t	en_fos;		/* fp operand segment selector */
	uint16_t	en_pad1;	/* padding */
	uint32_t	en_mxcsr;	/* SSE control/status register */
	uint32_t	en_mxcsr_mask;	/* valid bits in mxcsr */
};

struct envxmm64 {
	uint16_t	en_cw;		/* control word (16bits) */
	uint16_t	en_sw;		/* status word (16bits) */
	uint8_t		en_tw;		/* tag word (8bits) */
	uint8_t		en_zero;
	uint16_t	en_opcode;	/* opcode last executed (11 bits ) */
	uint64_t	en_rip;		/* fp instruction pointer */
	uint64_t	en_rdp;		/* fp operand pointer */
	uint32_t	en_mxcsr;	/* SSE control/status register */
	uint32_t	en_mxcsr_mask;	/* valid bits in mxcsr */
};

/* Contents of each SSE extended accumulator */
struct  xmmacc {
	uint8_t		xmm_bytes[16];
};

/* Contents of the upper 16 bytes of each AVX extended accumulator */
struct ymmacc {
	uint8_t		ymm_bytes[16];
};

/*
 * Floating point context. (i386 fxsave/fxrstor)
 * savexmm is a 512-byte structure
 */
struct  savexmm {
	struct	envxmm	sv_env;			/*  32 */
	struct {
		struct fpacc87	fp_acc;		/*     -- 10 */
		uint8_t		fp_pad[6];      /*     -- 6  */
	} sv_fp[8];				/* 128 */
	struct xmmacc	sv_xmm[8];		/* 128 */
	uint8_t			sv_pad[224];	/* 224 (padding) */
} __aligned(16);

/*
 * Floating point context. (amd64 fxsave/fxrstor)
 * savexmm64 is a 512-byte structure
 */
struct	savexmm64 {
	struct	envxmm64	sv_env;		/*  32 */
	struct {
		struct fpacc87	fp_acc;
		uint8_t		fp_pad[6];
	} sv_fp[8];				/* 128 */
	struct xmmacc	sv_xmm[8];		/* 128 */
	uint8_t			sv_pad[224];	/* 224 */
} __aligned(16);

/* xstate_hdr is a 64-byte structure */
struct	xstate_hdr {
	uint64_t	xstate_bv;
	uint64_t	xstate_xcomp_bv;
	uint8_t		xstate_rsrv0[8];
	uint8_t		xstate_rsrv[40];
};
#define	XSTATE_XCOMP_BV_COMPACT	(1ULL << 63)

/* savexmm_xstate is a 320-byte structure (64 + 256) */
struct	savexmm_xstate {
	struct xstate_hdr	sx_hd;
	struct ymmacc		sx_ymm[16];
};

/* saveymm is a 832-byte structure (i386) */
struct	saveymm {
	struct envxmm	sv_env;			/*  32 */
	struct {
		struct fpacc87	fp_acc;
		uint8_t		fp_pad[6];
	} sv_fp[8];				/* 128 */
	struct xmmacc	sv_xmm[16];		/* 256 */
	uint8_t			sv_pad[96];	/*  96 */
	struct savexmm_xstate	sv_xstate;	/* 320 */
} __aligned(64);

/* saveymm64 is a 832-byte structure (amd64) */
struct	saveymm64 {
	struct envxmm64	sv_env;			/*  32 */
	struct {
		struct fpacc87	fp_acc;
		int8_t		fp_pad[6];
	} sv_fp[8];				/* 128 */
	struct xmmacc	sv_xmm[16];		/* 256 */
	uint8_t			sv_pad[96];	/*  96 */
	struct savexmm_xstate	sv_xstate;	/* 320 */
} __aligned(64);

union	savefpu {
	struct	save87	sv_87;
	struct	savexmm	sv_xmm;
	struct  saveymm sv_ymm;
	struct	savexmm64	sv_xmm64;
	struct	saveymm64	sv_ymm64;
	char		sv_savearea[1024];	/* see mcontext_t */
};

/*
 * The hardware default control word for i387's and later coprocessors is
 * 0x37F, giving:
 *
 *	round to nearest
 *	64-bit precision
 *	all exceptions masked.
 *
 * We modify the affine mode bit and precision bits in this to give:
 *
 *	affine mode for 287's (if they work at all) (1 in bitfield 1<<12)
 *	53-bit precision (2 in bitfield 3<<8)
 *
 * 64-bit precision often gives bad results with high level languages
 * because it makes the results of calculations depend on whether
 * intermediate values are stored in memory or in FPU registers.
 */
#define	__INITIAL_NPXCW__	0x127F

#define __INITIAL_FPUCW__       0x037F	/* used by libm/arch/x86_64/fenv.c */
#define __INITIAL_FPUCW_I386__  0x127F
#define __INITIAL_MXCSR__       0x1F80	/* used by libm/arch/x86_64/fenv.c */
#define __INITIAL_MXCSR_MASK__  0xFFBF

#ifdef _KERNEL

struct proc;
struct trapframe;

extern uint32_t npx_mxcsr_mask; 

void	npxprobemask (void);
void	npxexit (void);
void	npxinit (void);
void	npxsave (union savefpu *addr);
#endif

#endif /* !_CPU_NPX_H_ */
