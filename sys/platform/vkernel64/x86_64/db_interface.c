/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * --
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/i386/i386/db_interface.c,v 1.48.2.1 2000/07/07 00:38:46 obrien Exp $
 */

/*
 * Interface to new debugger.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/cons.h>
#include <sys/thread.h>

#include <machine/cpu.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <ddb/ddb.h>

#include <sys/thread2.h>

#include <setjmp.h>

static jmp_buf *db_nofault = NULL;
extern jmp_buf	db_jmpbuf;

extern void	gdb_handle_exception (db_regs_t *, int, int);

int	db_active;
db_regs_t ddb_regs;

static jmp_buf	db_global_jmpbuf;
static int	db_global_jmpbuf_valid;

#ifdef __GNUC__
#define	rss() ({u_short ss; __asm __volatile("mov %%ss,%0" : "=r" (ss)); ss;})
#endif

/*
 *  kdb_trap - field a TRACE or BPT trap
 */
int
kdb_trap(int type, int code, struct x86_64_saved_state *regs)
{
	volatile int ddb_mode = !(boothowto & RB_GDB);

	/*
	 * XXX try to do nothing if the console is in graphics mode.
	 * Handle trace traps (and hardware breakpoints...) by ignoring
	 * them except for forgetting about them.  Return 0 for other
	 * traps to say that we haven't done anything.  The trap handler
	 * will usually panic.  We should handle breakpoint traps for
	 * our breakpoints by disarming our breakpoints and fixing up
	 * %eip.
	 */
	if (cons_unavail && ddb_mode) {
	    if (type == T_TRCTRAP) {
		regs->tf_rflags &= ~PSL_T;
		return (1);
	    }
	    return (0);
	}

	switch (type) {
	    case T_BPTFLT:	/* breakpoint */
	    case T_TRCTRAP:	/* debug exception */
		break;

	    default:
		/*
		 * XXX this is almost useless now.  In most cases,
		 * trap_fatal() has already printed a much more verbose
		 * message.  However, it is dangerous to print things in
		 * trap_fatal() - kprintf() might be reentered and trap.
		 * The debugger should be given control first.
		 */
		if (ddb_mode)
		    db_printf("kernel: type %d trap, code=%x\n", type, code);

		if (db_nofault) {
		    jmp_buf *no_fault = db_nofault;
		    db_nofault = NULL;
		    longjmp(*no_fault, 1);
		}
	}

	/*
	 * This handles unexpected traps in ddb commands, including calls to
	 * non-ddb functions.  db_nofault only applies to memory accesses by
	 * internal ddb commands.
	 */
	if (db_global_jmpbuf_valid)
	    longjmp(db_global_jmpbuf, 1);

	/*
	 * XXX We really should switch to a local stack here.
	 */
	ddb_regs = *regs;

	crit_enter();
	db_printf("\nCPU%d stopping CPUs: 0x%016jx\n",
	    mycpu->gd_cpuid, (uintmax_t)CPUMASK_LOWMASK(mycpu->gd_other_cpus));

	/* We stop all CPUs except ourselves (obviously) */
	stop_cpus(mycpu->gd_other_cpus);

	db_printf(" stopped\n");

	setjmp(db_global_jmpbuf);
	db_global_jmpbuf_valid = TRUE;
	db_active++;
	vcons_set_mode(1);
	if (ddb_mode) {
	    cndbctl(TRUE);
	    db_trap(type, code);
	    cndbctl(FALSE);
	} else
	    gdb_handle_exception(&ddb_regs, type, code);
	db_active--;
	vcons_set_mode(0);
	db_global_jmpbuf_valid = FALSE;

	db_printf("\nCPU%d restarting CPUs: 0x%016jx\n",
	    mycpu->gd_cpuid, (uintmax_t)CPUMASK_LOWMASK(stopped_cpus));

	/* Restart all the CPUs we previously stopped */
	if (CPUMASK_CMPMASKNEQ(stopped_cpus, mycpu->gd_other_cpus)) {
		db_printf("whoa, other_cpus: 0x%016jx, "
			  "stopped_cpus: 0x%016jx\n",
			  (uintmax_t)CPUMASK_LOWMASK(mycpu->gd_other_cpus),
			  (uintmax_t)CPUMASK_LOWMASK(stopped_cpus));
		panic("stop_cpus() failed");
	}
	restart_cpus(stopped_cpus);

	db_printf(" restarted\n");
	crit_exit();

	regs->tf_rip    = ddb_regs.tf_rip;
	regs->tf_rflags = ddb_regs.tf_rflags;
	regs->tf_rax    = ddb_regs.tf_rax;
	regs->tf_rcx    = ddb_regs.tf_rcx;
	regs->tf_rdx    = ddb_regs.tf_rdx;
	regs->tf_rbx    = ddb_regs.tf_rbx;

	regs->tf_rsp    = ddb_regs.tf_rsp;
	regs->tf_ss     = ddb_regs.tf_ss & 0xffff;

	regs->tf_rbp    = ddb_regs.tf_rbp;
	regs->tf_rsi    = ddb_regs.tf_rsi;
	regs->tf_rdi    = ddb_regs.tf_rdi;

	regs->tf_r8     = ddb_regs.tf_r8;
	regs->tf_r9     = ddb_regs.tf_r9;
	regs->tf_r10    = ddb_regs.tf_r10;
	regs->tf_r11    = ddb_regs.tf_r11;
	regs->tf_r12    = ddb_regs.tf_r12;
	regs->tf_r13    = ddb_regs.tf_r13;
	regs->tf_r14    = ddb_regs.tf_r14;
	regs->tf_r15    = ddb_regs.tf_r15;

	/* regs->tf_es     = ddb_regs.tf_es & 0xffff; */
	/* regs->tf_fs     = ddb_regs.tf_fs & 0xffff; */
	/* regs->tf_gs     = ddb_regs.tf_gs & 0xffff; */
	regs->tf_cs     = ddb_regs.tf_cs & 0xffff;
	/* regs->tf_ds     = ddb_regs.tf_ds & 0xffff; */
	return (1);
}

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vm_offset_t addr, size_t size, char *data)
{
	char	*src;

	db_nofault = &db_jmpbuf;

	src = (char *)addr;
	while (size-- > 0)
	    *data++ = *src++;

	db_nofault = NULL;
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vm_offset_t addr, size_t size, char *data)
{
	char	*dst;
#if 0
	vpte_t	*ptep0 = NULL;
	vpte_t	oldmap0 = 0;
	vm_offset_t	addr1;
	vpte_t	*ptep1 = NULL;
	vpte_t	oldmap1 = 0;
#endif

	db_nofault = &db_jmpbuf;
#if 0
	if (addr > trunc_page((vm_offset_t)btext) - size &&
	    addr < round_page((vm_offset_t)etext)) {

	    ptep0 = pmap_kpte(addr);
	    oldmap0 = *ptep0;
	    *ptep0 |= VPTE_RW;

	    /* Map another page if the data crosses a page boundary. */
	    if ((*ptep0 & PG_PS) == 0) {
		addr1 = trunc_page(addr + size - 1);
		if (trunc_page(addr) != addr1) {
		    ptep1 = pmap_kpte(addr1);
		    oldmap1 = *ptep1;
		    *ptep1 |= VPTE_RW;
		}
	    } else {
		addr1 = trunc_4mpage(addr + size - 1);
		if (trunc_4mpage(addr) != addr1) {
		    ptep1 = pmap_kpte(addr1);
		    oldmap1 = *ptep1;
		    *ptep1 |= VPTE_RW;
		}
	    }

	    cpu_invltlb();
	}
#endif

	dst = (char *)addr;

	while (size-- > 0)
	    *dst++ = *data++;

	db_nofault = NULL;

#if 0
	if (ptep0) {
	    *ptep0 = oldmap0;

	    if (ptep1)
		*ptep1 = oldmap1;

	    cpu_invltlb();
	}
#endif
}

/*
 * The debugger sometimes needs to know the actual KVM address represented
 * by the instruction pointer, stack pointer, or base pointer.  Normally
 * the actual KVM address is simply the contents of the register.  However,
 * if the debugger is entered from the BIOS or VM86 we need to figure out
 * the offset from the segment register.
 */
db_addr_t
PC_REGS(db_regs_t *regs)
{
    return(regs->tf_rip);
}

db_addr_t
SP_REGS(db_regs_t *regs)
{
    return(regs->tf_rsp);
}

db_addr_t
BP_REGS(db_regs_t *regs)
{
    return(regs->tf_rbp);
}

/*
 * XXX
 * Move this to machdep.c and allow it to be called if any debugger is
 * installed.
 */
void
Debugger(const char *msg)
{
	static volatile u_char in_Debugger;

	/*
	 * XXX
	 * Do nothing if the console is in graphics mode.  This is
	 * OK if the call is for the debugger hotkey but not if the call
	 * is a weak form of panicing.
	 */
	if (cons_unavail && !(boothowto & RB_GDB))
	    return;

	if (!in_Debugger) {
	    in_Debugger = 1;
	    db_printf("Debugger(\"%s\")\n", msg);
	    breakpoint();
	    in_Debugger = 0;
	}
}
