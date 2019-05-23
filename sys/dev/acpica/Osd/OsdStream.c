/*-
 * Copyright (c) 2000 Michael Smith
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
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdStream.c,v 1.4 2004/04/14 03:39:08 njl Exp $
 */

/*
 * Stream I/O
 */

#include "acpi.h"
#include "accommon.h"

#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <dev/acpica/acpivar.h>

int acpi_silence_all = 0;
TUNABLE_INT("debug.acpi.silence_all", &acpi_silence_all);
SYSCTL_INT(_debug_acpi, OID_AUTO, silence_all, CTLFLAG_RW,
    &acpi_silence_all, 0, "Silence ACPI messages");

void
AcpiOsPrintf(const char *Format, ...)
{
    va_list	ap;

    if (acpi_silence_all == 0) {
	    va_start(ap, Format);
	    kvprintf(Format, ap);
	    va_end(ap);
    }
}

void
AcpiOsVprintf(const char *Format, va_list Args)
{
    if (acpi_silence_all == 0) {
	    kvprintf(Format, Args);
    }
}
