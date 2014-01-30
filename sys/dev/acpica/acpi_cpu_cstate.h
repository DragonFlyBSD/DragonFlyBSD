/*-
 * Copyright (c) 2003-2005 Nate Lawson (SDG)
 * Copyright (c) 2001 Michael Smith
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
 *
 * $FreeBSD: src/sys/dev/acpica/acpi_cpu.c,v 1.72 2008/04/12 12:06:00 rpaulo Exp $
 */

#ifndef __ACPI_CPU_CSTATE_H__
#define __ACPI_CPU_CSTATE_H__

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

struct acpi_cst_cx {
    uint32_t		type;		/* C1-3+. */
    uint32_t		trans_lat;	/* Transition latency (usec). */
    int			preamble;	/* ACPI_CST_CX_PREAMBLE_ */
    uint32_t		flags;		/* ACPI_CST_CX_FLAG_ */
    uint32_t		md_arg0;	/* machine depend arg */
    void		(*enter)(const struct acpi_cst_cx *);
    bus_space_tag_t	btag;
    bus_space_handle_t	bhand;

    struct resource	*res;
    ACPI_GENERIC_ADDRESS gas;
    int			rid;
    int			res_type;
    uint32_t		power;		/* Power consumed (mW). */
};

#define ACPI_CST_CX_PREAMBLE_NONE	0
#define ACPI_CST_CX_PREAMBLE_WBINVD	1	/* flush cache */
#define ACPI_CST_CX_PREAMBLE_BM_ARB	2	/* disable BM_ARB */

#define ACPI_CST_CX_FLAG_BM_STS		0x1	/* check BM_STS */

extern int	acpi_cst_quirks;	/* ACPI_CST_QUIRK_ */

#define ACPI_CST_QUIRK_NO_C3		0x1	/* C3(+) not usable. */
#define ACPI_CST_QUIRK_NO_BM		0x2	/* No bus mastering. */

int	acpi_cst_md_cx_setup(struct acpi_cst_cx *);

#endif	/* !__ACPI_CPU_CSTATE_H__ */
