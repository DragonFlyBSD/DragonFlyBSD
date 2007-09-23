/*-
 * Copyright (c) 1982, 1990 The Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)genassym.c	5.11 (Berkeley) 5/10/91
 * $FreeBSD: src/sys/i386/i386/genassym.c,v 1.86.2.3 2002/03/03 05:42:49 nyan Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/genassym.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $ 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>
#include <sys/interrupt.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/lock.h>
#include <sys/resourcevar.h>
#include <machine/frame.h>
#include <machine/bootinfo.h>
#include <machine/tss.h>
#include <sys/vmmeter.h>
#include <sys/machintr.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <net/if.h>
#include <netinet/in.h>
#include <vfs/nfs/nfsv2.h>
#include <vfs/nfs/rpcv2.h>
#include <vfs/nfs/nfs.h>
#include <vfs/nfs/nfsdiskless.h>

#include <machine/segments.h>
#include <machine/sigframe.h>
#include <machine/globaldata.h>
#include <machine/pcb.h>

ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));
ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));

ASSYM(UPAGES, UPAGES);
ASSYM(PAGE_SIZE, PAGE_SIZE);
ASSYM(NPTEPG, NPTEPG);
ASSYM(NPDEPG, NPDEPG);
ASSYM(PDESIZE, PDESIZE);
ASSYM(PTESIZE, PTESIZE);
ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_MASK, PAGE_MASK);
ASSYM(PDRSHIFT, PDRSHIFT);
ASSYM(USRSTACK, USRSTACK);
ASSYM(KERNBASE,	KERNBASE);

ASSYM(MAXCOMLEN, MAXCOMLEN);
ASSYM(EFAULT, EFAULT);
ASSYM(ENAMETOOLONG, ENAMETOOLONG);
ASSYM(VM_MAXUSER_ADDRESS, VM_MAXUSER_ADDRESS);
ASSYM(GD_CURTHREAD, offsetof(struct mdglobaldata, mi.gd_curthread));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));

ASSYM(SIGF_HANDLER, offsetof(struct sigframe, sf_ahu.sf_handler));
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(KCSEL, GSEL(GCODE_SEL, SEL_KPL));
ASSYM(KDSEL, GSEL(GDATA_SEL, SEL_KPL));

ASSYM(TD_PRI, offsetof(struct thread, td_pri));

ASSYM(TDPRI_CRIT, TDPRI_CRIT);
ASSYM(TDPRI_INT_SUPPORT, TDPRI_INT_SUPPORT);
