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
#include <sys/user.h>
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
#include <machine/vm86.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>

ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));
ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));
ASSYM(PM_ACTIVE_LOCK, offsetof(struct pmap, pm_active_lock));

ASSYM(LWP_VMSPACE, offsetof(struct lwp, lwp_vmspace));

ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_LWP, offsetof(struct thread, td_lwp));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_SP, offsetof(struct thread, td_sp));
ASSYM(TD_PRI, offsetof(struct thread, td_pri));
ASSYM(TD_MACH, offsetof(struct thread, td_mach));
ASSYM(TD_WCHAN, offsetof(struct thread, td_wchan));
ASSYM(TD_NEST_COUNT, offsetof(struct thread, td_nest_count));
ASSYM(TD_CRITCOUNT, offsetof(struct thread, td_critcount));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TDF_RUNNING, TDF_RUNNING);
ASSYM(TDF_USINGFP, TDF_USINGFP);
ASSYM(TDF_KERNELFP, TDF_KERNELFP);
ASSYM(MACHINTR_INTREN, offsetof(struct machintr_abi, intr_enable));

ASSYM(TD_SAVEFPU, offsetof(struct thread, td_mach) + offsetof(struct md_thread, mtd_savefpu));

ASSYM(TDPRI_INT_SUPPORT, TDPRI_INT_SUPPORT);
ASSYM(CPULOCK_EXCLBIT, CPULOCK_EXCLBIT);
ASSYM(CPULOCK_EXCL, CPULOCK_EXCL);
ASSYM(CPULOCK_INCR, CPULOCK_INCR);
ASSYM(CPULOCK_CNTMASK, CPULOCK_CNTMASK);

ASSYM(V_TRAP, offsetof(struct vmmeter, v_trap));
ASSYM(V_SYSCALL, offsetof(struct vmmeter, v_syscall));
ASSYM(V_SENDSYS, offsetof(struct vmmeter, v_sendsys));
ASSYM(V_WAITSYS, offsetof(struct vmmeter, v_waitsys));
ASSYM(V_INTR, offsetof(struct vmmeter, v_intr));
ASSYM(V_IPI, offsetof(struct vmmeter, v_ipi));
ASSYM(V_TIMER, offsetof(struct vmmeter, v_timer));
ASSYM(V_FORWARDED_INTS, offsetof(struct vmmeter, v_forwarded_ints));
ASSYM(V_FORWARDED_HITS, offsetof(struct vmmeter, v_forwarded_hits));
ASSYM(V_FORWARDED_MISSES, offsetof(struct vmmeter, v_forwarded_misses));
ASSYM(UPAGES, UPAGES);
ASSYM(PAGE_SIZE, PAGE_SIZE);
ASSYM(NPTEPG, NPTEPG);
ASSYM(NPDEPG, NPDEPG);
ASSYM(PDESIZE, PDESIZE);
ASSYM(PTESIZE, PTESIZE);
ASSYM(SMP_MAXCPU, SMP_MAXCPU);
ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_MASK, PAGE_MASK);
ASSYM(PDRSHIFT, PDRSHIFT);
ASSYM(USRSTACK, USRSTACK);
ASSYM(VM_MAX_USER_ADDRESS, VM_MAX_USER_ADDRESS);
ASSYM(KERNBASE, KERNBASE);
ASSYM(MCLBYTES, MCLBYTES);
ASSYM(PCB_CR3, offsetof(struct pcb, pcb_cr3));
ASSYM(PCB_EDI, offsetof(struct pcb, pcb_edi));
ASSYM(PCB_ESI, offsetof(struct pcb, pcb_esi));
ASSYM(PCB_EBP, offsetof(struct pcb, pcb_ebp));
ASSYM(PCB_ESP, offsetof(struct pcb, pcb_esp));
ASSYM(PCB_EBX, offsetof(struct pcb, pcb_ebx));
ASSYM(PCB_EIP, offsetof(struct pcb, pcb_eip));
ASSYM(TSS_ESP0, offsetof(struct i386tss, tss_esp0));

ASSYM(PCB_USERLDT, offsetof(struct pcb, pcb_ldt));

ASSYM(PCB_DR0, offsetof(struct pcb, pcb_dr0));
ASSYM(PCB_DR1, offsetof(struct pcb, pcb_dr1));
ASSYM(PCB_DR2, offsetof(struct pcb, pcb_dr2));
ASSYM(PCB_DR3, offsetof(struct pcb, pcb_dr3));
ASSYM(PCB_DR6, offsetof(struct pcb, pcb_dr6));
ASSYM(PCB_DR7, offsetof(struct pcb, pcb_dr7));
ASSYM(PCB_DBREGS, PCB_DBREGS);
ASSYM(PCB_EXT, offsetof(struct pcb, pcb_ext));

ASSYM(PCB_SPARE, offsetof(struct pcb, __pcb_spare));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_SAVEFPU, offsetof(struct pcb, pcb_save));
ASSYM(PCB_SAVEFPU_SIZE, sizeof(union savefpu));
ASSYM(PCB_SAVE87_SIZE, sizeof(struct save87));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_ONFAULT_SP, offsetof(struct pcb, pcb_onfault_sp));

ASSYM(PCB_SIZE, sizeof(struct pcb));

ASSYM(TF_XFLAGS, offsetof(struct trapframe, tf_xflags));
ASSYM(TF_TRAPNO, offsetof(struct trapframe, tf_trapno));
ASSYM(TF_ERR, offsetof(struct trapframe, tf_err));
ASSYM(TF_CS, offsetof(struct trapframe, tf_cs));
ASSYM(TF_EFLAGS, offsetof(struct trapframe, tf_eflags));
ASSYM(SIGF_HANDLER, offsetof(struct sigframe, sf_ahu.sf_handler));
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));
ASSYM(UC_EFLAGS, offsetof(ucontext_t, uc_mcontext.mc_eflags));
ASSYM(ENOENT, ENOENT);
ASSYM(EFAULT, EFAULT);
ASSYM(ENAMETOOLONG, ENAMETOOLONG);
ASSYM(MAXPATHLEN, MAXPATHLEN);
ASSYM(BOOTINFO_SIZE, sizeof(struct bootinfo));
ASSYM(BI_VERSION, offsetof(struct bootinfo, bi_version));
ASSYM(BI_KERNELNAME, offsetof(struct bootinfo, bi_kernelname));
ASSYM(BI_NFS_DISKLESS, offsetof(struct bootinfo, bi_nfs_diskless));
ASSYM(BI_ENDCOMMON, offsetof(struct bootinfo, bi_endcommon));
ASSYM(NFSDISKLESS_SIZE, sizeof(struct nfs_diskless));
ASSYM(BI_SIZE, offsetof(struct bootinfo, bi_size));
ASSYM(BI_SYMTAB, offsetof(struct bootinfo, bi_symtab));
ASSYM(BI_ESYMTAB, offsetof(struct bootinfo, bi_esymtab));
ASSYM(BI_KERNEND, offsetof(struct bootinfo, bi_kernend));

ASSYM(GD_CURTHREAD, offsetof(struct mdglobaldata, mi.gd_curthread));
ASSYM(GD_CPUID, offsetof(struct mdglobaldata, mi.gd_cpuid));
ASSYM(GD_CPUMASK, offsetof(struct mdglobaldata, mi.gd_cpumask));
ASSYM(GD_CNT, offsetof(struct mdglobaldata, mi.gd_cnt));
ASSYM(GD_PRIVATE_TSS, offsetof(struct mdglobaldata, gd_private_tss));
ASSYM(GD_INTR_NESTING_LEVEL, offsetof(struct mdglobaldata, mi.gd_intr_nesting_level));
ASSYM(GD_REQFLAGS, offsetof(struct mdglobaldata, mi.gd_reqflags));

ASSYM(GD_CURRENTLDT, offsetof(struct mdglobaldata, gd_currentldt));

ASSYM(RQF_IPIQ, RQF_IPIQ);
ASSYM(RQF_INTPEND, RQF_INTPEND);
ASSYM(RQF_AST_OWEUPC, RQF_AST_OWEUPC);
ASSYM(RQF_AST_SIGNAL, RQF_AST_SIGNAL);
ASSYM(RQF_AST_USER_RESCHED, RQF_AST_USER_RESCHED);
ASSYM(RQF_AST_LWKT_RESCHED, RQF_AST_LWKT_RESCHED);
ASSYM(RQF_TIMER, RQF_TIMER);
ASSYM(RQF_AST_MASK, RQF_AST_MASK);

ASSYM(FIRST_SOFTINT, FIRST_SOFTINT);
ASSYM(MDGLOBALDATA_BASEALLOC_PAGES, MDGLOBALDATA_BASEALLOC_PAGES);

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
ASSYM(GD_CMAP1, offsetof(struct mdglobaldata, gd_CMAP1));
ASSYM(GD_CMAP2, offsetof(struct mdglobaldata, gd_CMAP2));
ASSYM(GD_CMAP3, offsetof(struct mdglobaldata, gd_CMAP3));
ASSYM(GD_PMAP1, offsetof(struct mdglobaldata, gd_PMAP1));
ASSYM(GD_CADDR1, offsetof(struct mdglobaldata, gd_CADDR1));
ASSYM(GD_CADDR2, offsetof(struct mdglobaldata, gd_CADDR2));
ASSYM(GD_CADDR3, offsetof(struct mdglobaldata, gd_CADDR3));
ASSYM(GD_PADDR1, offsetof(struct mdglobaldata, gd_PADDR1));

ASSYM(PS_IDLESTACK, offsetof(struct privatespace, idlestack));
ASSYM(PS_IDLESTACK_PAGE, offsetof(struct privatespace, idlestack) / PAGE_SIZE);
ASSYM(PS_IDLESTACK_TOP, sizeof(struct privatespace));
ASSYM(PS_SIZEOF, sizeof(struct privatespace));

ASSYM(KCSEL, GSEL(GCODE_SEL, SEL_KPL));
ASSYM(KDSEL, GSEL(GDATA_SEL, SEL_KPL));
ASSYM(KPSEL, GSEL(GPRIV_SEL, SEL_KPL));

ASSYM(BC32SEL, GSEL(GBIOSCODE32_SEL, SEL_KPL));
ASSYM(VM86_FRAMESIZE, sizeof(struct vm86frame));

ASSYM(LA_EOI, offsetof(struct LAPIC, eoi));

ASSYM(IOAPIC_IRQI_ADDR, offsetof(struct ioapic_irqinfo, io_addr));
ASSYM(IOAPIC_IRQI_IDX, offsetof(struct ioapic_irqinfo, io_idx));
ASSYM(IOAPIC_IRQI_FLAGS, offsetof(struct ioapic_irqinfo, io_flags));
ASSYM(IOAPIC_IRQI_SIZE, sizeof(struct ioapic_irqinfo));
ASSYM(IOAPIC_IRQI_SZSHIFT, IOAPIC_IRQI_SZSHIFT);
ASSYM(IOAPIC_IRQI_FLAG_LEVEL, IOAPIC_IRQI_FLAG_LEVEL);
ASSYM(IOAPIC_IRQI_FLAG_MASKED, IOAPIC_IRQI_FLAG_MASKED);
