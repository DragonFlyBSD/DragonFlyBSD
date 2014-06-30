/*-
 * Copyright (c) 1982, 1990 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
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
 * $DragonFly: src/sys/platform/pc64/amd64/genassym.c,v 1.2 2008/08/29 17:07:10 dillon Exp $
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
/*#include <machine/bootinfo.h>*/
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
#include <machine/specialreg.h>
#include <machine/pcb.h>
#include <machine/smp.h>

ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));
ASSYM(PM_ACTIVE_LOCK, offsetof(struct pmap, pm_active_lock));

ASSYM(LWP_VMSPACE, offsetof(struct lwp, lwp_vmspace));
ASSYM(GD_CURTHREAD, offsetof(struct mdglobaldata, mi.gd_curthread));
ASSYM(GD_CPUID, offsetof(struct mdglobaldata, mi.gd_cpuid));
ASSYM(GD_CPUMASK, offsetof(struct mdglobaldata, mi.gd_cpumask));

ASSYM(CPULOCK_EXCLBIT, CPULOCK_EXCLBIT);
ASSYM(CPULOCK_EXCL, CPULOCK_EXCL);
ASSYM(CPULOCK_INCR, CPULOCK_INCR);
ASSYM(CPULOCK_CNTMASK, CPULOCK_CNTMASK);

ASSYM(PCB_R15, offsetof(struct pcb, pcb_r15));
ASSYM(PCB_R14, offsetof(struct pcb, pcb_r14));
ASSYM(PCB_R13, offsetof(struct pcb, pcb_r13));
ASSYM(PCB_R12, offsetof(struct pcb, pcb_r12));
ASSYM(PCB_RBP, offsetof(struct pcb, pcb_rbp));
ASSYM(PCB_RSP, offsetof(struct pcb, pcb_rsp));
ASSYM(PCB_RBX, offsetof(struct pcb, pcb_rbx));
ASSYM(PCB_RIP, offsetof(struct pcb, pcb_rip));
ASSYM(TSS_RSP0, offsetof(struct x86_64tss, tss_rsp0));

ASSYM(PCB_DR0, offsetof(struct pcb, pcb_dr0));
ASSYM(PCB_DR1, offsetof(struct pcb, pcb_dr1));
ASSYM(PCB_DR2, offsetof(struct pcb, pcb_dr2));
ASSYM(PCB_DR3, offsetof(struct pcb, pcb_dr3));
ASSYM(PCB_DR6, offsetof(struct pcb, pcb_dr6));
ASSYM(PCB_DR7, offsetof(struct pcb, pcb_dr7));
ASSYM(PCB_DBREGS, PCB_DBREGS);
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_FSBASE, offsetof(struct pcb, pcb_fsbase));
ASSYM(PCB_GSBASE, offsetof(struct pcb, pcb_gsbase));
ASSYM(PCB_SAVEFPU, offsetof(struct pcb, pcb_save));

ASSYM(PCB_SAVEFPU_SIZE, sizeof(union savefpu));
ASSYM(SIGF_HANDLER, offsetof(struct sigframe, sf_ahu.sf_handler));
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(TD_LWP, offsetof(struct thread, td_lwp));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_SP, offsetof(struct thread, td_sp));
ASSYM(TD_PRI, offsetof(struct thread, td_pri));
ASSYM(TD_CRITCOUNT, offsetof(struct thread, td_critcount));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_SAVEFPU, offsetof(struct thread, td_savefpu));
ASSYM(TDF_RUNNING, TDF_RUNNING);
ASSYM(GD_NPXTHREAD, offsetof(struct mdglobaldata, gd_npxthread));
