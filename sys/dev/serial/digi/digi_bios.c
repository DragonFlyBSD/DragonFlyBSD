/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * 
 * $DragonFly: src/sys/dev/serial/digi/digi_bios.c,v 1.1 2004/12/22 08:42:47 joerg Exp $
 */

#include <sys/param.h>

#include <dev/serial/digi/digi_bios.h>
#include <dev/serial/digi/CX.bios.h>
#include <dev/serial/digi/CX.fepos.h>
#include <dev/serial/digi/CX_PCI.bios.h>
#include <dev/serial/digi/CX_PCI.fepos.h>
#include <dev/serial/digi/EPCX.bios.h>
#include <dev/serial/digi/EPCX.fepos.h>
#include <dev/serial/digi/EPCX_PCI.bios.h>
#include <dev/serial/digi/EPCX_PCI.fepos.h>
#include <dev/serial/digi/Xe.bios.h>
#include <dev/serial/digi/Xe.fepos.h>
#include <dev/serial/digi/Xem.bios.h>
#include <dev/serial/digi/Xem.fepos.h>
#include <dev/serial/digi/Xr.bios.h>
#include <dev/serial/digi/Xr.fepos.h>

struct digi_bios digi_bioses[] = {
	{ "CX", CX_bios, sizeof(CX_bios), CX_fepos, sizeof(CX_fepos) },
	{ "CX_PCI", CX_PCI_bios, sizeof(CX_PCI_bios),
	  CX_PCI_fepos, sizeof(CX_PCI_fepos) },
	{ "EPCX", EPCX_bios, sizeof(EPCX_bios),
	  EPCX_fepos, sizeof(EPCX_fepos) },
	{ "EPCX_PCI", EPCX_PCI_bios, sizeof(EPCX_PCI_bios),
	  EPCX_PCI_fepos, sizeof(EPCX_PCI_fepos) },
	{ "Xe", Xe_bios, sizeof(Xe_bios), Xe_fepos, sizeof(Xe_fepos) },
	{ "Xem", Xe_bios, sizeof(Xem_bios), Xem_fepos, sizeof(Xem_fepos) },
	{ "Xr", Xr_bios, sizeof(Xr_bios), Xr_fepos, sizeof(Xr_fepos) },
	{ NULL, NULL, 0, NULL, 0 }
};
