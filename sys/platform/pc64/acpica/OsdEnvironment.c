/*-
 * Copyright (c) 2000,2001 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * $FreeBSD: src/sys/i386/acpica/OsdEnvironment.c,v 1.10 2004/05/06 02:18:58 njl Exp $
 */

/*
 * Environmental and ACPI Tables (partial)
 */

#include <sys/types.h>
#include <sys/linker_set.h>
#include <sys/sysctl.h>

#include <machine/vmparam.h>

#include "acpi.h"

static u_long acpi_root_phys;

SYSCTL_ULONG(_machdep, OID_AUTO, acpi_root, CTLFLAG_RD, &acpi_root_phys, 0,
    "The physical address of the RSDP");

ACPI_STATUS
AcpiOsInitialize(void)
{
	return (AE_OK);
}

ACPI_STATUS
AcpiOsTerminate(void)
{
	return (AE_OK);
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{
	ACPI_SIZE ptr;
	ACPI_STATUS status;
	u_long acpi_root;

	if (acpi_root_phys == 0) {
		/*
		 * The loader passes the physical address at which it found the
		 * RSDP in a hint.  We try to recover this before searching
		 * manually here.
		 */
		if (kgetenv_ulong("hint.acpi.0.rsdp", &acpi_root) == 1) {
			acpi_root_phys = acpi_root;
		} else {
			status = AcpiFindRootPointer(&ptr);
			if (ACPI_SUCCESS(status))
				acpi_root_phys = ptr;
		}
	}

	return (acpi_root_phys);
}
