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

#include <machine_base/apic/apicreg.h>
#include <machine_base/apic/ioapic_abi.h>
#include <machine/segments.h>
#include <machine/sigframe.h>
#include <machine/globaldata.h>
#include <machine/specialreg.h>
#include <machine/pcb.h>
#include <machine/pmap.h>

ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));
ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));
ASSYM(PM_ACTIVE_LOCK, offsetof(struct pmap, pm_active_lock));

ASSYM(LWP_VMSPACE, offsetof(struct lwp, lwp_vmspace));
ASSYM(V_IPI, offsetof(struct vmmeter, v_ipi));
ASSYM(V_TIMER, offsetof(struct vmmeter, v_timer));
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

ASSYM(V_TRAP, offsetof(struct vmmeter, v_trap));
ASSYM(V_SYSCALL, offsetof(struct vmmeter, v_syscall));
ASSYM(V_SENDSYS, offsetof(struct vmmeter, v_sendsys));
ASSYM(V_WAITSYS, offsetof(struct vmmeter, v_waitsys));
ASSYM(V_INTR, offsetof(struct vmmeter, v_intr));
ASSYM(V_FORWARDED_INTS, offsetof(struct vmmeter, v_forwarded_ints));
ASSYM(V_FORWARDED_HITS, offsetof(struct vmmeter, v_forwarded_hits));
ASSYM(V_FORWARDED_MISSES, offsetof(struct vmmeter, v_forwarded_misses));

ASSYM(MAXCOMLEN, MAXCOMLEN);
ASSYM(EFAULT, EFAULT);
ASSYM(ENAMETOOLONG, ENAMETOOLONG);
ASSYM(VM_MAX_USER_ADDRESS, VM_MAX_USER_ADDRESS);

ASSYM(GD_CURTHREAD, offsetof(struct mdglobaldata, mi.gd_curthread));
ASSYM(GD_CNT, offsetof(struct mdglobaldata, mi.gd_cnt));
ASSYM(GD_CPUID, offsetof(struct mdglobaldata, mi.gd_cpuid));
ASSYM(GD_CPUMASK, offsetof(struct mdglobaldata, mi.gd_cpumask));
ASSYM(GD_SAMPLE_PC, offsetof(struct mdglobaldata, mi.gd_sample_pc));

ASSYM(PCB_CR3, offsetof(struct pcb, pcb_cr3));
ASSYM(PCB_R15, offsetof(struct pcb, pcb_r15));
ASSYM(PCB_R14, offsetof(struct pcb, pcb_r14));
ASSYM(PCB_R13, offsetof(struct pcb, pcb_r13));
ASSYM(PCB_R12, offsetof(struct pcb, pcb_r12));
ASSYM(PCB_RSI, offsetof(struct pcb, pcb_rsi));
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
ASSYM(PCB_EXT, offsetof(struct pcb, pcb_ext));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_ONFAULT_SP, offsetof(struct pcb, pcb_onfault_sp));
ASSYM(PCB_FSBASE, offsetof(struct pcb, pcb_fsbase));
ASSYM(PCB_GSBASE, offsetof(struct pcb, pcb_gsbase));
ASSYM(PCB_SAVEFPU, offsetof(struct pcb, pcb_save));

ASSYM(PCB_SIZE, sizeof(struct pcb));
ASSYM(PCB_SAVEFPU_SIZE, sizeof(union savefpu));

ASSYM(TF_R15, offsetof(struct trapframe, tf_r15));
ASSYM(TF_R14, offsetof(struct trapframe, tf_r14));
ASSYM(TF_R13, offsetof(struct trapframe, tf_r13));
ASSYM(TF_R12, offsetof(struct trapframe, tf_r12));
ASSYM(TF_R11, offsetof(struct trapframe, tf_r11));
ASSYM(TF_R10, offsetof(struct trapframe, tf_r10));
ASSYM(TF_R9, offsetof(struct trapframe, tf_r9));
ASSYM(TF_R8, offsetof(struct trapframe, tf_r8));
ASSYM(TF_RDI, offsetof(struct trapframe, tf_rdi));
ASSYM(TF_RSI, offsetof(struct trapframe, tf_rsi));
ASSYM(TF_RBP, offsetof(struct trapframe, tf_rbp));
ASSYM(TF_RBX, offsetof(struct trapframe, tf_rbx));
ASSYM(TF_RDX, offsetof(struct trapframe, tf_rdx));
ASSYM(TF_RCX, offsetof(struct trapframe, tf_rcx));
ASSYM(TF_RAX, offsetof(struct trapframe, tf_rax));

ASSYM(TF_TRAPNO, offsetof(struct trapframe, tf_trapno));
ASSYM(TF_XFLAGS, offsetof(struct trapframe, tf_xflags));
ASSYM(TF_ADDR, offsetof(struct trapframe, tf_addr));
ASSYM(TF_ERR, offsetof(struct trapframe, tf_err));
ASSYM(TF_FLAGS, offsetof(struct trapframe, tf_flags));

ASSYM(TF_RIP, offsetof(struct trapframe, tf_rip));
ASSYM(TF_CS, offsetof(struct trapframe, tf_cs));
ASSYM(TF_RFLAGS, offsetof(struct trapframe, tf_rflags));
ASSYM(TF_RSP, offsetof(struct trapframe, tf_rsp));
ASSYM(TF_SS, offsetof(struct trapframe, tf_ss));
ASSYM(TF_SIZE, sizeof(struct trapframe));

ASSYM(SIGF_HANDLER, offsetof(struct sigframe, sf_ahu.sf_handler));
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_LWP, offsetof(struct thread, td_lwp));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_SP, offsetof(struct thread, td_sp));
ASSYM(TD_PRI, offsetof(struct thread, td_pri));
ASSYM(TD_CRITCOUNT, offsetof(struct thread, td_critcount));
ASSYM(TD_MACH, offsetof(struct thread, td_mach));
ASSYM(TD_WCHAN, offsetof(struct thread, td_wchan));
ASSYM(TD_NEST_COUNT, offsetof(struct thread, td_nest_count));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_TYPE, offsetof(struct thread, td_type));

ASSYM(TD_SAVEFPU, offsetof(struct thread, td_savefpu));
ASSYM(TDF_RUNNING, TDF_RUNNING);
ASSYM(TDF_USINGFP, TDF_USINGFP);
ASSYM(TDF_KERNELFP, TDF_KERNELFP);

ASSYM(FIRST_SOFTINT, FIRST_SOFTINT);
ASSYM(MDGLOBALDATA_BASEALLOC_PAGES, MDGLOBALDATA_BASEALLOC_PAGES);

ASSYM(GD_PRIVATE_TSS, offsetof(struct mdglobaldata, gd_private_tss));
ASSYM(GD_SCRATCH_RSP, offsetof(struct mdglobaldata, gd_scratch_rsp));
ASSYM(GD_USER_FS, offsetof(struct mdglobaldata, gd_user_fs));
ASSYM(GD_USER_GS, offsetof(struct mdglobaldata, gd_user_gs));
ASSYM(GD_INTR_NESTING_LEVEL, offsetof(struct mdglobaldata, mi.gd_intr_nesting_level));

ASSYM(GD_IPENDING, offsetof(struct mdglobaldata, gd_ipending));
ASSYM(GD_SPENDING, offsetof(struct mdglobaldata, gd_spending));
ASSYM(GD_COMMON_TSS, offsetof(struct mdglobaldata, gd_common_tss));
ASSYM(GD_COMMON_TSSD, offsetof(struct mdglobaldata, gd_common_tssd));
ASSYM(GD_TSS_GDT, offsetof(struct mdglobaldata, gd_tss_gdt));
ASSYM(GD_NPXTHREAD, offsetof(struct mdglobaldata, gd_npxthread));
ASSYM(GD_FPU_LOCK, offsetof(struct mdglobaldata, gd_fpu_lock));
ASSYM(GD_SAVEFPU, offsetof(struct mdglobaldata, gd_savefpu));
ASSYM(GD_OTHER_CPUS, offsetof(struct mdglobaldata, mi.gd_other_cpus));
ASSYM(GD_SS_EFLAGS, offsetof(struct mdglobaldata, gd_ss_eflags));
ASSYM(GD_REQFLAGS, offsetof(struct mdglobaldata, mi.gd_reqflags));

ASSYM(RQF_IPIQ, RQF_IPIQ);
ASSYM(RQF_INTPEND, RQF_INTPEND);
ASSYM(RQF_AST_OWEUPC, RQF_AST_OWEUPC);
ASSYM(RQF_AST_SIGNAL, RQF_AST_SIGNAL);
ASSYM(RQF_AST_USER_RESCHED, RQF_AST_USER_RESCHED);
ASSYM(RQF_AST_LWKT_RESCHED, RQF_AST_LWKT_RESCHED);
ASSYM(RQF_TIMER, RQF_TIMER);
ASSYM(RQF_AST_MASK, RQF_AST_MASK);
ASSYM(RQF_QUICKRET, RQF_QUICKRET);

ASSYM(LA_EOI, offsetof(struct LAPIC, eoi));

ASSYM(KCSEL, GSEL(GCODE_SEL, SEL_KPL));
ASSYM(KDSEL, GSEL(GDATA_SEL, SEL_KPL));
ASSYM(KUCSEL, GSEL(GUCODE_SEL, SEL_UPL));
ASSYM(KUDSEL, GSEL(GUDATA_SEL, SEL_UPL));
/*ASSYM(SEL_RPL_MASK, SEL_RPL_MASK);*/

ASSYM(MSR_GSBASE, MSR_GSBASE);
ASSYM(MSR_KGSBASE, MSR_KGSBASE);
ASSYM(MSR_FSBASE, MSR_FSBASE);

ASSYM(MACHINTR_INTREN, offsetof(struct machintr_abi, intr_enable));

ASSYM(TDPRI_INT_SUPPORT, TDPRI_INT_SUPPORT);
ASSYM(CPULOCK_EXCLBIT, CPULOCK_EXCLBIT);
ASSYM(CPULOCK_EXCL, CPULOCK_EXCL);
ASSYM(CPULOCK_INCR, CPULOCK_INCR);
ASSYM(CPULOCK_CNTMASK, CPULOCK_CNTMASK);

ASSYM(CPUMASK_ELEMENTS, CPUMASK_ELEMENTS);

ASSYM(IOAPIC_IRQI_ADDR, offsetof(struct ioapic_irqinfo, io_addr));
ASSYM(IOAPIC_IRQI_IDX, offsetof(struct ioapic_irqinfo, io_idx));
ASSYM(IOAPIC_IRQI_FLAGS, offsetof(struct ioapic_irqinfo, io_flags));
ASSYM(IOAPIC_IRQI_SIZE, sizeof(struct ioapic_irqinfo));
ASSYM(IOAPIC_IRQI_SZSHIFT, IOAPIC_IRQI_SZSHIFT);
ASSYM(IOAPIC_IRQI_FLAG_LEVEL, IOAPIC_IRQI_FLAG_LEVEL);
ASSYM(IOAPIC_IRQI_FLAG_MASKED, IOAPIC_IRQI_FLAG_MASKED);
