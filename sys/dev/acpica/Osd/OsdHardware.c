/*-
 * Copyright (c) 2000, 2001 Michael Smith
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
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdHardware.c,v 1.13 2004/04/14 03:39:08 njl Exp $
 */

/*
 * 6.7 : Hardware Abstraction
 */

#include "acpi.h"

#include <sys/bus.h>
#include <bus/pci/pci_cfgreg.h>
#include <bus/pci/pcireg.h>

/*
 * ACPICA's rather gung-ho approach to hardware resource ownership is a little
 * troublesome insofar as there is no easy way for us to know in advance 
 * exactly which I/O resources it's going to want to use.
 * 
 * In order to deal with this, we ignore resource ownership entirely, and simply
 * use the native I/O space accessor functionality.  This is Evil, but it works.
 *
 * XXX use an intermediate #define for the tag/handle
 */

#ifdef __i386__
#define ACPI_BUS_SPACE_IO	I386_BUS_SPACE_IO
#define ACPI_BUS_HANDLE		0
#endif
#ifdef __x86_64__
#define ACPI_BUS_SPACE_IO	X86_64_BUS_SPACE_IO
#define ACPI_BUS_HANDLE		0
#endif

ACPI_STATUS
AcpiOsReadPort(ACPI_IO_ADDRESS InPort, UINT32 *Value, UINT32 Width)
{
    switch (Width) {
    case 8:
        *Value = bus_space_read_1(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, InPort);
        break;
    case 16:
        *Value = bus_space_read_2(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, InPort);
        break;
    case 32:
        *Value = bus_space_read_4(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, InPort);
        break;
    }

    return (AE_OK);
}

ACPI_STATUS
AcpiOsWritePort(ACPI_IO_ADDRESS OutPort, UINT32 Value, UINT32 Width)
{
    switch (Width) {
    case 8:
        bus_space_write_1(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, OutPort, Value);
        break;
    case 16:
        bus_space_write_2(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, OutPort, Value);
        break;
    case 32:
        bus_space_write_4(ACPI_BUS_SPACE_IO, ACPI_BUS_HANDLE, OutPort, Value);
        break;
    }

    return (AE_OK);
}

ACPI_STATUS
AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register, UINT64 *Value,
    UINT32 Width)
{
    int bytes = Width / 8;

    if (Width == 64)
	return (AE_SUPPORT);

    if (!pci_cfgregopen())
        return (AE_NOT_EXIST);

    *Value = pci_cfgregread(PciId->Bus, PciId->Device,
    				      PciId->Function, Register, bytes);
    *Value &= (1 << (bytes * 8)) - 1;

    return (AE_OK);
}


ACPI_STATUS
AcpiOsWritePciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register,
    UINT64 Value, UINT32 Width)
{
    if (Width == 64)
	return (AE_SUPPORT);

    if (!pci_cfgregopen())
    	return (AE_NOT_EXIST);

    pci_cfgregwrite(PciId->Bus, PciId->Device, PciId->Function, Register,
    		    (u_int32_t)Value, Width / 8); /* XXX casting */

    return (AE_OK);
}
