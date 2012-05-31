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
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdDebug.c,v 1.9 2004/09/02 04:28:05 njl Exp $
 * $DragonFly: src/sys/dev/acpica5/Osd/OsdDebug.c,v 1.6 2007/01/17 17:31:19 y0netan1 Exp $
 */

/*
 * 6.8 : Debugging support
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <ddb/ddb.h>
#include <ddb/db_output.h>

#include "acpi.h"
#include "accommon.h"
#include "acpivar.h"
#include "acdebug.h"

ACPI_STATUS
AcpiOsGetLine(char *Buffer, UINT32 BufferLength, UINT32 *BytesRead)
{
#ifdef DDB
    char *cp;

    cp = Buffer;
    if (db_readline(Buffer, BufferLength) > 0)
        while (*cp != '\0' && *cp != '\n' && *cp != '\r')
            cp++;
    *cp = '\0';
    if (BytesRead != NULL)
        *BytesRead = cp - Buffer;
    return (AE_OK);
#else
    kprintf("AcpiOsGetLine called but no input support");
    return (AE_NOT_EXIST);
#endif /* DDB */
}

#if 0	/* unused for ages */
void
AcpiOsDbgAssert(void *FailedAssertion, void *FileName, UINT32 LineNumber,
    char *Message)
{
    kprintf("ACPI: %s:%d - %s\n", (char *)FileName, LineNumber, Message);
    kprintf("ACPI: assertion  %s\n", (char *)FailedAssertion);
}
#endif

ACPI_STATUS
AcpiOsSignal(UINT32 Function, void *Info)
{
    ACPI_SIGNAL_FATAL_INFO	*fatal;
    
    switch (Function) {
    case ACPI_SIGNAL_FATAL:
	fatal = (ACPI_SIGNAL_FATAL_INFO *)Info;
	kprintf("ACPI fatal signal, type 0x%x  code 0x%x  argument 0x%x",
	      fatal->Type, fatal->Code, fatal->Argument);
	Debugger("AcpiOsSignal");
	break;
	
    case ACPI_SIGNAL_BREAKPOINT:
#ifdef ACPI_DEBUG
	Debugger((char *)Info);
#endif
	break;

    default:
	return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
}

#ifdef ACPI_DEBUGGER
void
acpi_EnterDebugger(void)
{
    ACPI_PARSE_OBJECT	obj;
    static int		initted = 0;

    if (!initted) {
	kprintf("Initialising ACPICA debugger...\n");
	AcpiDbInitialize();
	initted = 1;
    }

    kprintf("Entering ACPICA debugger...\n");
    AcpiDbUserCommands('A', &obj);
}
#endif /* ACPI_DEBUGGER */
