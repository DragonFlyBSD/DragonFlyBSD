/*
 * Copyright (c) 1993 Christopher G. Demetriou
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * $FreeBSD: src/sys/i386/include/cputypes.h,v 1.13.2.1 2000/09/30 03:32:20 ps Exp $
 * $DragonFly: src/sys/cpu/i386/include/cputypes.h,v 1.4 2006/11/07 06:43:22 dillon Exp $
 */

#ifndef _CPU_CPUTYPES_H_
#define	_CPU_CPUTYPES_H_

/*
 * Classes of processor.
 */
#define	CPUCLASS_286	0
#define	CPUCLASS_386	1
#define	CPUCLASS_486	2
#define	CPUCLASS_586	3
#define	CPUCLASS_686	4

/*
 * Kinds of processor.
 */
#define	CPU_286		0	/* Intel 80286 */
#define	CPU_386SX	1	/* Intel 80386SX */
#define	CPU_386		2	/* Intel 80386DX */
#define	CPU_486SX	3	/* Intel 80486SX */
#define	CPU_486		4	/* Intel 80486DX */
#define	CPU_586		5	/* Intel P.....m (I hate lawyers; it's TM) */
#define	CPU_686		6	/* Pentium Pro */
#define	CPU_BLUE	7	/* IBM BlueLighting CPU */
#define	CPU_NX586	8	/* NexGen (now AMD) 586 */
#define	CPU_PII		9	/* Intel Pentium II */
#define	CPU_PIII	10	/* Intel Pentium III */
#define	CPU_P4		11	/* Intel Pentium 4 */
#define	CPU_GEODE1100	12	/* NS Geode SC1100 */

/*
 * Vendors of processor.
 */
#define	CPU_VENDOR_NSC		0x100b		/* NSC */
#define	CPU_VENDOR_IBM		0x1014		/* IBM */
#define	CPU_VENDOR_AMD		0x1022		/* AMD */
#define	CPU_VENDOR_SIS		0x1039		/* SiS */
#define	CPU_VENDOR_UMC		0x1060		/* UMC */
#define	CPU_VENDOR_NEXGEN	0x1074		/* Nexgen */
#define	CPU_VENDOR_IDT		0x111d		/* Centaur/IDT/VIA */
#define	CPU_VENDOR_TRANSMETA	0x1279		/* Transmeta */
#define	CPU_VENDOR_INTEL	0x8086		/* Intel */
#define	CPU_VENDOR_RISE		0xdead2bad	/* Rise */
#define	CPU_VENDOR_CENTAUR	CPU_VENDOR_IDT

#ifndef LOCORE
struct cpu_nameclass {
	char	*cpu_name;
	int	cpu_class;
};

extern int	cpu;
extern int	cpu_class;

extern void	additional_cpu_info(const char *);
#endif

#endif /* !_CPU_CPUTYPES_H_ */
