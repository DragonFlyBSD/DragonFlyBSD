/*
 * Copyright (c) 2004, Joerg Sonnenberger <joerg@bec.de>
 * All rights reserved.
 * Copyright (c) 1994,1995 Stefan Esser.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $DragonFly: src/sys/bus/pci/pci_isab.c,v 1.3 2005/01/17 17:50:21 joerg Exp $
 */

#include "opt_pci.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/resource.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include "pcib_private.h"

static	void	chipset_attach(device_t dev, int unit);
		
#ifndef PCI_QUIET

struct condmsg {
    unsigned char	port;
    unsigned char	mask;
    unsigned char	value;
    char		flags;
    const char		*text;
};

#define	M_XX 0	/* end of list */
#define M_EQ 1  /* mask and return true if equal */
#define M_NE 2  /* mask and return true if not equal */
#define M_TR 3  /* don't read config, always true */
#define M_EN 4	/* mask and print "enabled" if true, "disabled" if false */
#define M_NN 5	/* opposite sense of M_EN */

static const struct condmsg conf82425ex[] =
{
    { 0x00, 0x00, 0x00, M_TR, "\tClock " },
    { 0x50, 0x06, 0x00, M_EQ, "25" },
    { 0x50, 0x06, 0x02, M_EQ, "33" },
    { 0x50, 0x04, 0x04, M_EQ, "??", },
    { 0x00, 0x00, 0x00, M_TR, "MHz, L1 Cache " },
    { 0x50, 0x01, 0x00, M_EQ, "Disabled\n" },
    { 0x50, 0x09, 0x01, M_EQ, "Write-through\n" },
    { 0x50, 0x09, 0x09, M_EQ, "Write-back\n" },

    { 0x00, 0x00, 0x00, M_TR, "\tL2 Cache " },
    { 0x52, 0x07, 0x00, M_EQ, "Disabled" },
    { 0x52, 0x0f, 0x01, M_EQ, "64KB Write-through" },
    { 0x52, 0x0f, 0x02, M_EQ, "128KB Write-through" },
    { 0x52, 0x0f, 0x03, M_EQ, "256KB Write-through" },
    { 0x52, 0x0f, 0x04, M_EQ, "512KB Write-through" },
    { 0x52, 0x0f, 0x01, M_EQ, "64KB Write-back" },
    { 0x52, 0x0f, 0x02, M_EQ, "128KB Write-back" },
    { 0x52, 0x0f, 0x03, M_EQ, "256KB Write-back" },
    { 0x52, 0x0f, 0x04, M_EQ, "512KB Write-back" },
    { 0x53, 0x01, 0x00, M_EQ, ", 3-" },
    { 0x53, 0x01, 0x01, M_EQ, ", 2-" },
    { 0x53, 0x06, 0x00, M_EQ, "3-3-3" },
    { 0x53, 0x06, 0x02, M_EQ, "2-2-2" },
    { 0x53, 0x06, 0x04, M_EQ, "1-1-1" },
    { 0x53, 0x06, 0x06, M_EQ, "?-?-?" },
    { 0x53, 0x18, 0x00, M_EQ, "/4-2-2-2\n" },
    { 0x53, 0x18, 0x08, M_EQ, "/3-2-2-2\n" },
    { 0x53, 0x18, 0x10, M_EQ, "/?-?-?-?\n" },
    { 0x53, 0x18, 0x18, M_EQ, "/2-1-1-1\n" },

    { 0x56, 0x00, 0x00, M_TR, "\tDRAM: " },
    { 0x56, 0x02, 0x02, M_EQ, "Fast Code Read, " },
    { 0x56, 0x04, 0x04, M_EQ, "Fast Data Read, " },
    { 0x56, 0x08, 0x08, M_EQ, "Fast Write, " },
    { 0x57, 0x20, 0x20, M_EQ, "Pipelined CAS" },
    { 0x57, 0x2e, 0x00, M_NE, "\n\t" },
    { 0x57, 0x00, 0x00, M_TR, "Timing: RAS: " },
    { 0x57, 0x07, 0x00, M_EQ, "4" },
    { 0x57, 0x07, 0x01, M_EQ, "3" },
    { 0x57, 0x07, 0x02, M_EQ, "2" },
    { 0x57, 0x07, 0x04, M_EQ, "1.5" },
    { 0x57, 0x07, 0x05, M_EQ, "1" },
    { 0x57, 0x00, 0x00, M_TR, " Clocks, CAS Read: " },
    { 0x57, 0x18, 0x00, M_EQ, "3/1", },
    { 0x57, 0x18, 0x00, M_EQ, "2/1", },
    { 0x57, 0x18, 0x00, M_EQ, "1.5/0.5", },
    { 0x57, 0x18, 0x00, M_EQ, "1/1", },
    { 0x57, 0x00, 0x00, M_TR, ", CAS Write: " },
    { 0x57, 0x20, 0x00, M_EQ, "2/1", },
    { 0x57, 0x20, 0x20, M_EQ, "1/1", },
    { 0x57, 0x00, 0x00, M_TR, "\n" },

    { 0x40, 0x01, 0x01, M_EQ, "\tCPU-to-PCI Byte Merging\n" },
    { 0x40, 0x02, 0x02, M_EQ, "\tCPU-to-PCI Bursting\n" },
    { 0x40, 0x04, 0x04, M_EQ, "\tPCI Posted Writes\n" },
    { 0x40, 0x20, 0x00, M_EQ, "\tDRAM Parity Disabled\n" },

    { 0x48, 0x03, 0x01, M_EQ, "\tPCI IDE controller: Primary (1F0h-1F7h,3F6h,3F7h)" },
    { 0x48, 0x03, 0x02, M_EQ, "\tPCI IDE controller: Secondary (170h-177h,376h,377h)" },
    { 0x4d, 0x01, 0x01, M_EQ, "\tRTC (70-77h)\n" },
    { 0x4d, 0x02, 0x02, M_EQ, "\tKeyboard (60,62,64,66h)\n" },
    { 0x4d, 0x08, 0x08, M_EQ, "\tIRQ12/M Mouse Function\n" },

/* end marker */
    { 0 }
};

static const struct condmsg conf82424zx[] =
{
    { 0x00, 0x00, 0x00, M_TR, "\tCPU: " },
    { 0x50, 0xe0, 0x00, M_EQ, "486DX" },
    { 0x50, 0xe0, 0x20, M_EQ, "486SX" },
    { 0x50, 0xe0, 0x40, M_EQ, "486DX2 or 486DX4" },
    { 0x50, 0xe0, 0x80, M_EQ, "Overdrive (writeback)" },

    { 0x00, 0x00, 0x00, M_TR, ", bus=" },
    { 0x50, 0x03, 0x00, M_EQ, "25MHz" },
    { 0x50, 0x03, 0x01, M_EQ, "33MHz" },
    { 0x53, 0x01, 0x01, M_TR, ", CPU->Memory posting "},
    { 0x53, 0x01, 0x00, M_EQ, "OFF" },
    { 0x53, 0x01, 0x01, M_EQ, "ON" },

    { 0x56, 0x30, 0x00, M_NE, "\n\tWarning:" },
    { 0x56, 0x20, 0x00, M_NE, " NO cache parity!" },
    { 0x56, 0x10, 0x00, M_NE, " NO DRAM parity!" },
    { 0x55, 0x04, 0x04, M_EQ, "\n\tWarning: refresh OFF! " },

    { 0x00, 0x00, 0x00, M_TR, "\n\tCache: " },
    { 0x52, 0x01, 0x00, M_EQ, "None" },
    { 0x52, 0xc1, 0x01, M_EQ, "64KB" },
    { 0x52, 0xc1, 0x41, M_EQ, "128KB" },
    { 0x52, 0xc1, 0x81, M_EQ, "256KB" },
    { 0x52, 0xc1, 0xc1, M_EQ, "512KB" },
    { 0x52, 0x03, 0x01, M_EQ, " writethrough" },
    { 0x52, 0x03, 0x03, M_EQ, " writeback" },

    { 0x52, 0x01, 0x01, M_EQ, ", cache clocks=" },
    { 0x52, 0x05, 0x01, M_EQ, "3-1-1-1" },
    { 0x52, 0x05, 0x05, M_EQ, "2-1-1-1" },

    { 0x00, 0x00, 0x00, M_TR, "\n\tDRAM:" },
    { 0x55, 0x43, 0x00, M_NE, " page mode" },
    { 0x55, 0x02, 0x02, M_EQ, " code fetch" },
    { 0x55, 0x43, 0x43, M_EQ, "," },
    { 0x55, 0x43, 0x42, M_EQ, " and" },
    { 0x55, 0x40, 0x40, M_EQ, " read" },
    { 0x55, 0x03, 0x03, M_EQ, " and" },
    { 0x55, 0x43, 0x41, M_EQ, " and" },
    { 0x55, 0x01, 0x01, M_EQ, " write" },
    { 0x55, 0x43, 0x00, M_NE, "," },

    { 0x00, 0x00, 0x00, M_TR, " memory clocks=" },
    { 0x55, 0x20, 0x00, M_EQ, "X-2-2-2" },
    { 0x55, 0x20, 0x20, M_EQ, "X-1-2-1" },

    { 0x00, 0x00, 0x00, M_TR, "\n\tCPU->PCI: posting " },
    { 0x53, 0x02, 0x00, M_NE, "ON" },
    { 0x53, 0x02, 0x00, M_EQ, "OFF" },
    { 0x00, 0x00, 0x00, M_TR, ", burst mode " },
    { 0x54, 0x02, 0x00, M_NE, "ON" },
    { 0x54, 0x02, 0x00, M_EQ, "OFF" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tPCI->Memory: posting " },
    { 0x54, 0x01, 0x00, M_NE, "ON" },
    { 0x54, 0x01, 0x00, M_EQ, "OFF" },

    { 0x00, 0x00, 0x00, M_TR, "\n" },

/* end marker */
    { 0 }
};

static const struct condmsg conf82434lx[] =
{
    { 0x00, 0x00, 0x00, M_TR, "\tCPU: " },
    { 0x50, 0xe3, 0x82, M_EQ, "Pentium, 60MHz" },
    { 0x50, 0xe3, 0x83, M_EQ, "Pentium, 66MHz" },
    { 0x50, 0xe3, 0xa2, M_EQ, "Pentium, 90MHz" },
    { 0x50, 0xe3, 0xa3, M_EQ, "Pentium, 100MHz" },
    { 0x50, 0xc2, 0x82, M_NE, "(unknown)" },
    { 0x50, 0x04, 0x00, M_EQ, " (primary cache OFF)" },

    { 0x53, 0x01, 0x01, M_TR, ", CPU->Memory posting "},
    { 0x53, 0x01, 0x01, M_NE, "OFF" },
    { 0x53, 0x01, 0x01, M_EQ, "ON" },

    { 0x53, 0x08, 0x00, M_NE, ", read around write"},

    { 0x70, 0x04, 0x00, M_EQ, "\n\tWarning: Cache parity disabled!" },
    { 0x57, 0x20, 0x00, M_NE, "\n\tWarning: DRAM parity mask!" },
    { 0x57, 0x01, 0x00, M_EQ, "\n\tWarning: refresh OFF! " },

    { 0x00, 0x00, 0x00, M_TR, "\n\tCache: " },
    { 0x52, 0x01, 0x00, M_EQ, "None" },
    { 0x52, 0x81, 0x01, M_EQ, "" },
    { 0x52, 0xc1, 0x81, M_EQ, "256KB" },
    { 0x52, 0xc1, 0xc1, M_EQ, "512KB" },
    { 0x52, 0x03, 0x01, M_EQ, " writethrough" },
    { 0x52, 0x03, 0x03, M_EQ, " writeback" },

    { 0x52, 0x01, 0x01, M_EQ, ", cache clocks=" },
    { 0x52, 0x21, 0x01, M_EQ, "3-2-2-2/4-2-2-2" },
    { 0x52, 0x21, 0x21, M_EQ, "3-1-1-1" },

    { 0x52, 0x01, 0x01, M_EQ, "\n\tCache flags: " },
    { 0x52, 0x11, 0x11, M_EQ, " cache-all" },
    { 0x52, 0x09, 0x09, M_EQ, " byte-control" },
    { 0x52, 0x05, 0x05, M_EQ, " powersaver" },

    { 0x00, 0x00, 0x00, M_TR, "\n\tDRAM:" },
    { 0x57, 0x10, 0x00, M_EQ, " page mode" },

    { 0x00, 0x00, 0x00, M_TR, " memory clocks=" },
    { 0x57, 0xc0, 0x00, M_EQ, "X-4-4-4 (70ns)" },
    { 0x57, 0xc0, 0x40, M_EQ, "X-4-4-4/X-3-3-3 (60ns)" },
    { 0x57, 0xc0, 0x80, M_EQ, "???" },
    { 0x57, 0xc0, 0xc0, M_EQ, "X-3-3-3 (50ns)" },
    { 0x58, 0x02, 0x02, M_EQ, ", RAS-wait" },
    { 0x58, 0x01, 0x01, M_EQ, ", CAS-wait" },

    { 0x00, 0x00, 0x00, M_TR, "\n\tCPU->PCI: posting " },
    { 0x53, 0x02, 0x02, M_EQ, "ON" },
    { 0x53, 0x02, 0x00, M_EQ, "OFF" },
    { 0x00, 0x00, 0x00, M_TR, ", burst mode " },
    { 0x54, 0x02, 0x00, M_NE, "ON" },
    { 0x54, 0x02, 0x00, M_EQ, "OFF" },
    { 0x54, 0x04, 0x00, M_TR, ", PCI clocks=" },
    { 0x54, 0x04, 0x00, M_EQ, "2-2-2-2" },
    { 0x54, 0x04, 0x00, M_NE, "2-1-1-1" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tPCI->Memory: posting " },
    { 0x54, 0x01, 0x00, M_NE, "ON" },
    { 0x54, 0x01, 0x00, M_EQ, "OFF" },

    { 0x57, 0x01, 0x01, M_EQ, "\n\tRefresh:" },
    { 0x57, 0x03, 0x03, M_EQ, " CAS#/RAS#(Hidden)" },
    { 0x57, 0x03, 0x01, M_EQ, " RAS#Only" },
    { 0x57, 0x05, 0x05, M_EQ, " BurstOf4" },

    { 0x00, 0x00, 0x00, M_TR, "\n" },

/* end marker */
    { 0 }
};

static const struct condmsg conf82378[] =
{
    { 0x00, 0x00, 0x00, M_TR, "\tBus Modes:" },
    { 0x41, 0x04, 0x04, M_EQ, " Bus Park," },
    { 0x41, 0x02, 0x02, M_EQ, " Bus Lock," },
    { 0x41, 0x02, 0x00, M_EQ, " Resource Lock," },
    { 0x41, 0x01, 0x01, M_EQ, " GAT" },
    { 0x4d, 0x20, 0x20, M_EQ, "\n\tCoprocessor errors enabled" },
    { 0x4d, 0x10, 0x10, M_EQ, "\n\tMouse function enabled" },

    { 0x4e, 0x30, 0x10, M_EQ, "\n\tIDE controller: Primary (1F0h-1F7h,3F6h,3F7h)" },
    { 0x4e, 0x30, 0x30, M_EQ, "\n\tIDE controller: Secondary (170h-177h,376h,377h)" },
    { 0x4e, 0x28, 0x08, M_EQ, "\n\tFloppy controller: 3F0h,3F1h " },
    { 0x4e, 0x24, 0x04, M_EQ, "\n\tFloppy controller: 3F2h-3F7h " },
    { 0x4e, 0x28, 0x28, M_EQ, "\n\tFloppy controller: 370h,371h " },
    { 0x4e, 0x24, 0x24, M_EQ, "\n\tFloppy controller: 372h-377h " },
    { 0x4e, 0x02, 0x02, M_EQ, "\n\tKeyboard controller: 60h,62h,64h,66h" },
    { 0x4e, 0x01, 0x01, M_EQ, "\n\tRTC: 70h-77h" },

    { 0x4f, 0x80, 0x80, M_EQ, "\n\tConfiguration RAM: 0C00h,0800h-08FFh" },
    { 0x4f, 0x40, 0x40, M_EQ, "\n\tPort 92: enabled" },
    { 0x4f, 0x03, 0x00, M_EQ, "\n\tSerial Port A: COM1 (3F8h-3FFh)" },
    { 0x4f, 0x03, 0x01, M_EQ, "\n\tSerial Port A: COM2 (2F8h-2FFh)" },
    { 0x4f, 0x0c, 0x00, M_EQ, "\n\tSerial Port B: COM1 (3F8h-3FFh)" },
    { 0x4f, 0x0c, 0x04, M_EQ, "\n\tSerial Port B: COM2 (2F8h-2FFh)" },
    { 0x4f, 0x30, 0x00, M_EQ, "\n\tParallel Port: LPT1 (3BCh-3BFh)" },
    { 0x4f, 0x30, 0x04, M_EQ, "\n\tParallel Port: LPT2 (378h-37Fh)" },
    { 0x4f, 0x30, 0x20, M_EQ, "\n\tParallel Port: LPT3 (278h-27Fh)" },
    { 0x00, 0x00, 0x00, M_TR, "\n" },

/* end marker */
    { 0 }
};

static const struct condmsg conf82437fx[] = 
{
    /* PCON -- PCI Control Register */
    { 0x00, 0x00, 0x00, M_TR, "\tCPU Inactivity timer: " },
    { 0x50, 0xe0, 0xe0, M_EQ, "8" },
    { 0x50, 0xe0, 0xd0, M_EQ, "7" },
    { 0x50, 0xe0, 0xc0, M_EQ, "6" },
    { 0x50, 0xe0, 0xb0, M_EQ, "5" },
    { 0x50, 0xe0, 0xa0, M_EQ, "4" },
    { 0x50, 0xe0, 0x90, M_EQ, "3" },
    { 0x50, 0xe0, 0x80, M_EQ, "2" },
    { 0x50, 0xe0, 0x00, M_EQ, "1" },
    { 0x00, 0x00, 0x00, M_TR, " clocks\n\tPeer Concurrency: " },
    { 0x50, 0x08, 0x08, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tCPU-to-PCI Write Bursting: " },
    { 0x50, 0x04, 0x00, M_NN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tPCI Streaming: " },
    { 0x50, 0x02, 0x00, M_NN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tBus Concurrency: " },
    { 0x50, 0x01, 0x00, M_NN, 0 },

    /* CC -- Cache Control Regsiter */
    { 0x00, 0x00, 0x00, M_TR, "\n\tCache:" },
    { 0x52, 0xc0, 0x80, M_EQ, " 512K" },
    { 0x52, 0xc0, 0x40, M_EQ, " 256K" },
    { 0x52, 0xc0, 0x00, M_EQ, " NO" },
    { 0x52, 0x30, 0x00, M_EQ, " pipelined-burst" },
    { 0x52, 0x30, 0x10, M_EQ, " burst" },
    { 0x52, 0x30, 0x20, M_EQ, " asynchronous" },
    { 0x52, 0x30, 0x30, M_EQ, " dual-bank pipelined-burst" },
    { 0x00, 0x00, 0x00, M_TR, " secondary; L1 " },
    { 0x52, 0x01, 0x00, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n" },

    /* DRAMC -- DRAM Control Register */
    { 0x57, 0x07, 0x00, M_EQ, "Warning: refresh OFF!\n" },
    { 0x00, 0x00, 0x00, M_TR, "\tDRAM:" },
    { 0x57, 0xc0, 0x00, M_EQ, " no memory hole" },
    { 0x57, 0xc0, 0x40, M_EQ, " 512K-640K memory hole" },
    { 0x57, 0xc0, 0x80, M_EQ, " 15M-16M memory hole" },
    { 0x57, 0x07, 0x01, M_EQ, ", 50 MHz refresh" },
    { 0x57, 0x07, 0x02, M_EQ, ", 60 MHz refresh" },
    { 0x57, 0x07, 0x03, M_EQ, ", 66 MHz refresh" },

    /* DRAMT = DRAM Timing Register */
    { 0x00, 0x00, 0x00, M_TR, "\n\tRead burst timing: " },
    { 0x58, 0x60, 0x00, M_EQ, "x-4-4-4/x-4-4-4" },
    { 0x58, 0x60, 0x20, M_EQ, "x-3-3-3/x-4-4-4" },
    { 0x58, 0x60, 0x40, M_EQ, "x-2-2-2/x-3-3-3" },
    { 0x58, 0x60, 0x60, M_EQ, "???" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tWrite burst timing: " },
    { 0x58, 0x18, 0x00, M_EQ, "x-4-4-4" },
    { 0x58, 0x18, 0x08, M_EQ, "x-3-3-3" },
    { 0x58, 0x18, 0x10, M_EQ, "x-2-2-2" },
    { 0x58, 0x18, 0x18, M_EQ, "???" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tRAS-CAS delay: " },
    { 0x58, 0x04, 0x00, M_EQ, "3" },
    { 0x58, 0x04, 0x04, M_EQ, "2" },
    { 0x00, 0x00, 0x00, M_TR, " clocks\n" },

    /* end marker */
    { 0 }
};

static const struct condmsg conf82437vx[] = 
{
    /* PCON -- PCI Control Register */
    { 0x00, 0x00, 0x00, M_TR, "\n\tPCI Concurrency: " },
    { 0x50, 0x08, 0x08, M_EN, 0 },

    /* CC -- Cache Control Regsiter */
    { 0x00, 0x00, 0x00, M_TR, "\n\tCache:" },
    { 0x52, 0xc0, 0x80, M_EQ, " 512K" },
    { 0x52, 0xc0, 0x40, M_EQ, " 256K" },
    { 0x52, 0xc0, 0x00, M_EQ, " NO" },
    { 0x52, 0x30, 0x00, M_EQ, " pipelined-burst" },
    { 0x52, 0x30, 0x10, M_EQ, " burst" },
    { 0x52, 0x30, 0x20, M_EQ, " asynchronous" },
    { 0x52, 0x30, 0x30, M_EQ, " dual-bank pipelined-burst" },
    { 0x00, 0x00, 0x00, M_TR, " secondary; L1 " },
    { 0x52, 0x01, 0x00, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n" },

    /* DRAMC -- DRAM Control Register */
    { 0x57, 0x07, 0x00, M_EQ, "Warning: refresh OFF!\n" },
    { 0x00, 0x00, 0x00, M_TR, "\tDRAM:" },
    { 0x57, 0xc0, 0x00, M_EQ, " no memory hole" },
    { 0x57, 0xc0, 0x40, M_EQ, " 512K-640K memory hole" },
    { 0x57, 0xc0, 0x80, M_EQ, " 15M-16M memory hole" },
    { 0x57, 0x07, 0x01, M_EQ, ", 50 MHz refresh" },
    { 0x57, 0x07, 0x02, M_EQ, ", 60 MHz refresh" },
    { 0x57, 0x07, 0x03, M_EQ, ", 66 MHz refresh" },

    /* DRAMT = DRAM Timing Register */
    { 0x00, 0x00, 0x00, M_TR, "\n\tRead burst timing: " },
    { 0x58, 0x60, 0x00, M_EQ, "x-4-4-4/x-4-4-4" },
    { 0x58, 0x60, 0x20, M_EQ, "x-3-3-3/x-4-4-4" },
    { 0x58, 0x60, 0x40, M_EQ, "x-2-2-2/x-3-3-3" },
    { 0x58, 0x60, 0x60, M_EQ, "???" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tWrite burst timing: " },
    { 0x58, 0x18, 0x00, M_EQ, "x-4-4-4" },
    { 0x58, 0x18, 0x08, M_EQ, "x-3-3-3" },
    { 0x58, 0x18, 0x10, M_EQ, "x-2-2-2" },
    { 0x58, 0x18, 0x18, M_EQ, "???" },
    { 0x00, 0x00, 0x00, M_TR, "\n\tRAS-CAS delay: " },
    { 0x58, 0x04, 0x00, M_EQ, "3" },
    { 0x58, 0x04, 0x04, M_EQ, "2" },
    { 0x00, 0x00, 0x00, M_TR, " clocks\n" },

    /* end marker */
    { 0 }
};

static const struct condmsg conf82371fb[] =
{
    /* IORT -- ISA I/O Recovery Timer Register */
    { 0x00, 0x00, 0x00, M_TR, "\tI/O Recovery Timing: 8-bit " },
    { 0x4c, 0x40, 0x00, M_EQ, "3.5" },
    { 0x4c, 0x78, 0x48, M_EQ, "1" },
    { 0x4c, 0x78, 0x50, M_EQ, "2" },
    { 0x4c, 0x78, 0x58, M_EQ, "3" },
    { 0x4c, 0x78, 0x60, M_EQ, "4" },
    { 0x4c, 0x78, 0x68, M_EQ, "5" },
    { 0x4c, 0x78, 0x70, M_EQ, "6" },
    { 0x4c, 0x78, 0x78, M_EQ, "7" },
    { 0x4c, 0x78, 0x40, M_EQ, "8" },
    { 0x00, 0x00, 0x00, M_TR, " clocks, 16-bit " },
    { 0x4c, 0x04, 0x00, M_EQ, "3.5" },
    { 0x4c, 0x07, 0x05, M_EQ, "1" },
    { 0x4c, 0x07, 0x06, M_EQ, "2" },
    { 0x4c, 0x07, 0x07, M_EQ, "3" },
    { 0x4c, 0x07, 0x04, M_EQ, "4" },
    { 0x00, 0x00, 0x00, M_TR, " clocks\n" },

    /* XBCS -- X-Bus Chip Select Register */
    { 0x00, 0x00, 0x00, M_TR, "\tExtended BIOS: " },
    { 0x4e, 0x80, 0x80, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tLower BIOS: " },
    { 0x4e, 0x40, 0x40, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tCoprocessor IRQ13: " },
    { 0x4e, 0x20, 0x20, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tMouse IRQ12: " },
    { 0x4e, 0x10, 0x10, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n" },

    { 0x00, 0x00, 0x00, M_TR, "\tInterrupt Routing: " },
#define PIRQ(x, n) \
    { 0x00, 0x00, 0x00, M_TR, n ": " }, \
    { x, 0x80, 0x80, M_EQ, "disabled" }, \
    { x, 0xc0, 0x40, M_EQ, "[shared] " }, \
    { x, 0x8f, 0x03, M_EQ, "IRQ3" }, \
    { x, 0x8f, 0x04, M_EQ, "IRQ4" }, \
    { x, 0x8f, 0x05, M_EQ, "IRQ5" }, \
    { x, 0x8f, 0x06, M_EQ, "IRQ6" }, \
    { x, 0x8f, 0x07, M_EQ, "IRQ7" }, \
    { x, 0x8f, 0x09, M_EQ, "IRQ9" }, \
    { x, 0x8f, 0x0a, M_EQ, "IRQ10" }, \
    { x, 0x8f, 0x0b, M_EQ, "IRQ11" }, \
    { x, 0x8f, 0x0c, M_EQ, "IRQ12" }, \
    { x, 0x8f, 0x0e, M_EQ, "IRQ14" }, \
    { x, 0x8f, 0x0f, M_EQ, "IRQ15" }

    /* Interrupt routing */
    PIRQ(0x60, "A"),
    PIRQ(0x61, ", B"),
    PIRQ(0x62, ", C"),
    PIRQ(0x63, ", D"),
    PIRQ(0x70, "\n\t\tMB0"),
    PIRQ(0x71, ", MB1"),

    { 0x00, 0x00, 0x00, M_TR, "\n" },

#undef PIRQ

    /* XXX - do DMA routing, too? */
    { 0 }
};

static const struct condmsg conf82371fb2[] =
{
    /* IDETM -- IDE Timing Register */
    { 0x00, 0x00, 0x00, M_TR, "\tPrimary IDE: " },
    { 0x41, 0x80, 0x80, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n\tSecondary IDE: " },
    { 0x43, 0x80, 0x80, M_EN, 0 },
    { 0x00, 0x00, 0x00, M_TR, "\n" },

    /* end of list */
    { 0 }
};

static void
writeconfig (device_t dev, const struct condmsg *tbl)
{
    while (tbl->flags != M_XX) {
	const char *text = 0;

	if (tbl->flags == M_TR) {
	    text = tbl->text;
	} else {
	    unsigned char v = pci_read_config(dev, tbl->port, 1);
	    switch (tbl->flags) {
    case M_EQ:
		if ((v & tbl->mask) == tbl->value) text = tbl->text;
		break;
    case M_NE:
		if ((v & tbl->mask) != tbl->value) text = tbl->text;
		break;
    case M_EN:
		text = (v & tbl->mask) ? "enabled" : "disabled";
		break;
    case M_NN:
		text = (v & tbl->mask) ? "disabled" : "enabled";
	    }
	}
	if (text) printf ("%s", text);
	tbl++;
    }
}

#endif /* PCI_QUIET */

static void
chipset_attach (device_t dev, int unit)
{
#ifndef PCI_QUIET
	if (!bootverbose)
		return;

	switch (pci_get_devid(dev)) {
	case 0x04868086:
		writeconfig (dev, conf82425ex);
		break;
	case 0x04838086:
		writeconfig (dev, conf82424zx);
		break;
	case 0x04a38086:
		writeconfig (dev, conf82434lx);
		break;
	case 0x04848086:
		writeconfig (dev, conf82378);
		break;
	case 0x122d8086:
		writeconfig (dev, conf82437fx);
		break;
	case 0x70308086:
		writeconfig (dev, conf82437vx);
		break;
	case 0x70008086:
	case 0x122e8086:
		writeconfig (dev, conf82371fb);
		break;
	case 0x70108086:
	case 0x12308086:
		writeconfig (dev, conf82371fb2);
		break;
#if 0
	case 0x00011011: /* DEC 21050 */
	case 0x00221014: /* IBM xxx */
		writeconfig (dev, conf_pci2pci);
		break;
#endif
	};
#endif /* PCI_QUIET */
}

static const char *
eisab_match(device_t dev)
{
	switch (pci_get_devid(dev)) {
	case 0x04828086:
		/* Recognize this specifically, it has PCI-HOST class (!) */
		return ("Intel 82375EB PCI-EISA bridge");
	}
	if (pci_get_class(dev) == PCIC_BRIDGE
	    && pci_get_subclass(dev) == PCIS_BRIDGE_EISA)
		return pci_bridge_type(dev);

	return NULL;
}

static const char *
isab_match(device_t dev)
{
	unsigned	rev;

	switch (pci_get_devid(dev)) {
	case 0x04848086:
		rev = pci_get_revid(dev);
		if (rev == 3)
		    return ("Intel 82378ZB PCI to ISA bridge");
		return ("Intel 82378IB PCI to ISA bridge");
	case 0x122e8086:
		return ("Intel 82371FB PCI to ISA bridge");
	case 0x70008086:
		return ("Intel 82371SB PCI to ISA bridge");
	case 0x71108086:
		return ("Intel 82371AB PCI to ISA bridge");
	case 0x71988086:
		return ("Intel 82443MX PCI to ISA bridge");
	case 0x24108086:
		return ("Intel 82801AA (ICH) PCI to LPC bridge");
	case 0x24208086:
		return ("Intel 82801AB (ICH0) PCI to LPC bridge");
	case 0x24408086:
		return ("Intel 82801BA/BAM (ICH2) PCI to LPC bridge");
	case 0x26408086:
		return ("Intel 82801FB/FBW (ICH6) PCI to LPC bridge");
	case 0x26428086:
		return ("Intel 82801FR/FRW (ICH6) PCI to LPC bridge");

	/* NVIDIA -- vendor 0x10de */
	case 0x006010de:
		return ("NVIDIA nForce2 PCI to ISA bridge");
	
	/* VLSI -- vendor 0x1004 */
	case 0x00061004:
		return ("VLSI 82C593 PCI to ISA bridge");

	/* VIA Technologies -- vendor 0x1106 */
	case 0x05861106: /* south bridge section */
		return ("VIA 82C586 PCI-ISA bridge");
	case 0x05961106:
		return ("VIA 82C596B PCI-ISA bridge");
	case 0x06861106:
		return ("VIA 82C686 PCI-ISA bridge");

	/* AcerLabs -- vendor 0x10b9 */
	/* Funny : The datasheet told me vendor id is "10b8",sub-vendor */
	/* id is '10b9" but the register always shows "10b9". -Foxfair  */
	case 0x153310b9:
		return ("AcerLabs M1533 portable PCI-ISA bridge");
	case 0x154310b9:
		return ("AcerLabs M1543 desktop PCI-ISA bridge");

	/* SiS -- vendor 0x1039 */
	case 0x00081039:
		return ("SiS 85c503 PCI-ISA bridge");

	/* Cyrix -- vendor 0x1078 */
	case 0x00001078:
		return ("Cyrix Cx5510 PCI-ISA bridge");
	case 0x01001078:
		return ("Cyrix Cx5530 PCI-ISA bridge");

	/* NEC -- vendor 0x1033 */
	/* The "C-bus" is 16-bits bus on PC98. */
	case 0x00011033:
		return ("NEC 0001 PCI to PC-98 C-bus bridge");
	case 0x002c1033:
		return ("NEC 002C PCI to PC-98 C-bus bridge");
	case 0x003b1033:
		return ("NEC 003B PCI to PC-98 C-bus bridge");
	/* UMC United Microelectronics 0x1060 */
	case 0x886a1060:
		return ("UMC UM8886 ISA Bridge with EIDE");

	/* Cypress -- vendor 0x1080 */
	case 0xc6931080:
		if (pci_get_class(dev) == PCIC_BRIDGE
		    && pci_get_subclass(dev) == PCIS_BRIDGE_ISA)
			return ("Cypress 82C693 PCI-ISA bridge");
		break;

	/* ServerWorks -- vendor 0x1166 */
	case 0x02001166:
		return ("ServerWorks IB6566 PCI to ISA bridge");
	}

	if (pci_get_class(dev) == PCIC_BRIDGE
	    && pci_get_subclass(dev) == PCIS_BRIDGE_ISA)
		return pci_bridge_type(dev);

	return NULL;
}

static int
isab_probe(device_t dev)
{
	const char *desc;
	int	is_eisa;

	is_eisa = 0;
	desc = eisab_match(dev);
	if (desc)
		is_eisa = 1;
	else
		desc = isab_match(dev);
	if (desc) {
		/*
		 * For a PCI-EISA bridge, add both eisa and isa.
		 * Only add one instance of eisa or isa for now.
		 */
		device_set_desc_copy(dev, desc);
		if (is_eisa && !devclass_get_device(devclass_find("eisa"), 0))
			device_add_child(dev, "eisa", -1);

		if (!devclass_get_device(devclass_find("isa"), 0))
			device_add_child(dev, "isa", -1);
		return -1000;
	}
	return ENXIO;
}

static int
isab_attach(device_t dev)
{
	chipset_attach(dev, device_get_unit(dev));
	return bus_generic_attach(dev);
}

static device_method_t isab_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		isab_probe),
	DEVMETHOD(device_attach,	isab_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t isab_driver = {
	"isab",
	isab_methods,
	1,
};

devclass_t isab_devclass;

DRIVER_MODULE(isab, pci, isab_driver, isab_devclass, 0, 0);
