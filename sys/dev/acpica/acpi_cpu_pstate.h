/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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

#ifndef __ACPI_CPU_PSTATE_H__
#define __ACPI_CPU_PSTATE_H__

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

struct acpi_pstate {
	uint32_t		st_freq;
	uint32_t		st_power;
	uint32_t		st_xsit_lat;
	uint32_t		st_bm_lat;
	uint32_t		st_cval;
	uint32_t		st_sval;
};

struct acpi_pst_res {
	ACPI_GENERIC_ADDRESS	pr_gas;
	struct resource		*pr_res;
	int			pr_rid;
	bus_space_tag_t		pr_bt;
	bus_space_handle_t	pr_bh;
};

struct acpi_pst_md {
	int			(*pmd_check_csr)
				(const struct acpi_pst_res *,
				 const struct acpi_pst_res *);
	int			(*pmd_check_pstates)
				(const struct acpi_pstate *, int);

	int			(*pmd_init)
				(const struct acpi_pst_res *,
				 const struct acpi_pst_res *);

	int			(*pmd_set_pstate)
				(const struct acpi_pst_res *,
				 const struct acpi_pst_res *,
				 const struct acpi_pstate *);

	const struct acpi_pstate *(*pmd_get_pstate)
				(const struct acpi_pst_res *,
				 const struct acpi_pstate *, int);
};

const struct acpi_pst_md	*acpi_pst_md_probe(void);

#endif	/* !__ACPI_CPU_PSTATE_H__ */
