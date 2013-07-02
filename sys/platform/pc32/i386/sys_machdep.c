/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	from: @(#)sys_machdep.c	5.5 (Berkeley) 1/19/91
 * $FreeBSD: src/sys/i386/i386/sys_machdep.c,v 1.47.2.3 2002/10/07 17:20:00 jhb Exp $
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/memrange.h>

#include <vm/vm.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/pcb_ext.h>	/* pcb.h included by sys/user.h */
#include <machine/sysarch.h>
#include <machine/smp.h>
#include <machine/globaldata.h>	/* mdcpu */

#include <vm/vm_kern.h>		/* for kernel_map */

#include <sys/thread2.h>
#include <sys/mplock2.h>

#define MAX_LD 8192
#define LD_PER_PAGE 512
#define NEW_MAX_LD(num)  ((num + LD_PER_PAGE) & ~(LD_PER_PAGE-1))
#define SIZE_FROM_LARGEST_LD(num) (NEW_MAX_LD(num) << 3)



static int ki386_get_ldt(struct lwp *, char *, int *);
static int ki386_set_ldt(struct lwp *, char *, int *);
static int ki386_get_ioperm(struct lwp *, char *);
static int ki386_set_ioperm(struct lwp *, char *);
static int check_descs(union descriptor *, int);
int i386_extend_pcb(struct lwp *);

/*
 * sysarch_args(int op, char *params)
 *
 * MPALMOSTSAFE
 */
int
sys_sysarch(struct sysarch_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	int error = 0;

	get_mplock();

	switch(uap->op) {
	case I386_GET_LDT:
		error = ki386_get_ldt(lp, uap->parms, &uap->sysmsg_result);
		break;
	case I386_SET_LDT:
		error = ki386_set_ldt(lp, uap->parms, &uap->sysmsg_result);
		break;
	case I386_GET_IOPERM:
		error = ki386_get_ioperm(lp, uap->parms);
		break;
	case I386_SET_IOPERM:
		error = ki386_set_ioperm(lp, uap->parms);
		break;
	case I386_VM86:
		error = vm86_sysarch(lp, uap->parms);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	rel_mplock();
	return (error);
}

int
i386_extend_pcb(struct lwp *lp)
{
	int i, offset;
	u_long *addr;
	struct pcb_ext *ext;
	struct soft_segment_descriptor ssd = {
		0,			/* segment base address (overwritten) */
		ctob(IOPAGES + 1) - 1,	/* length */
		SDT_SYS386TSS,		/* segment type */
		0,			/* priority level */
		1,			/* descriptor present */
		0, 0,
		0,			/* default 32 size */
		0			/* granularity */
	};

	ext = (struct pcb_ext *)kmem_alloc(&kernel_map, ctob(IOPAGES+1));
	if (ext == NULL)
		return (ENOMEM);
	bzero(ext, sizeof(struct pcb_ext)); 
	ext->ext_tss.tss_esp0 = (unsigned)((char *)lp->lwp_thread->td_pcb - 16);
	ext->ext_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	/*
	 * The last byte of the i/o map must be followed by an 0xff byte.
	 * We arbitrarily allocate 16 bytes here, to keep the starting
	 * address on a doubleword boundary.
	 */
	offset = PAGE_SIZE - 16;
	ext->ext_tss.tss_ioopt = 
	    (offset - offsetof(struct pcb_ext, ext_tss)) << 16;
	ext->ext_iomap = (caddr_t)ext + offset;
	ext->ext_vm86.vm86_intmap = (caddr_t)ext + offset - 32;

	addr = (u_long *)ext->ext_vm86.vm86_intmap;
	for (i = 0; i < (ctob(IOPAGES) + 32 + 16) / sizeof(u_long); i++)
		*addr++ = ~0;

	ssd.ssd_base = (unsigned)&ext->ext_tss;
	ssd.ssd_limit -= offsetof(struct pcb_ext, ext_tss);
	ssdtosd(&ssd, &ext->ext_tssd);

	/* 
	 * Put the new TSS where the switch code can find it.  Do
	 * a forced switch to ourself to activate it.
	 */
	crit_enter();
	lp->lwp_thread->td_pcb->pcb_ext = ext;
	lp->lwp_thread->td_switch(lp->lwp_thread);
	crit_exit();
	
	return 0;
}

static int
ki386_set_ioperm(struct lwp *lp, char *args)
{
	int i, error;
	struct i386_ioperm_args ua;
	char *iomap;

	if ((error = copyin(args, &ua, sizeof(struct i386_ioperm_args))) != 0)
		return (error);

	if ((error = priv_check_cred(lp->lwp_thread->td_ucred, PRIV_ROOT, 0)) != 0)
		return (error);
	if (securelevel > 0)
		return (EPERM);
	/*
	 * XXX 
	 * While this is restricted to root, we should probably figure out
	 * whether any other driver is using this i/o address, as so not to
	 * cause confusion.  This probably requires a global 'usage registry'.
	 */

	if (lp->lwp_thread->td_pcb->pcb_ext == 0)
		if ((error = i386_extend_pcb(lp)) != 0)
			return (error);
	iomap = (char *)lp->lwp_thread->td_pcb->pcb_ext->ext_iomap;

	if (ua.start + ua.length > IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);

	for (i = ua.start; i < ua.start + ua.length; i++) {
		if (ua.enable) 
			iomap[i >> 3] &= ~(1 << (i & 7));
		else
			iomap[i >> 3] |= (1 << (i & 7));
	}
	return (error);
}

static int
ki386_get_ioperm(struct lwp *lp, char *args)
{
	int i, state, error;
	struct i386_ioperm_args ua;
	char *iomap;

	if ((error = copyin(args, &ua, sizeof(struct i386_ioperm_args))) != 0)
		return (error);
	if (ua.start >= IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);

	if (lp->lwp_thread->td_pcb->pcb_ext == 0) {
		ua.length = 0;
		goto done;
	}

	iomap = (char *)lp->lwp_thread->td_pcb->pcb_ext->ext_iomap;

	i = ua.start;
	state = (iomap[i >> 3] >> (i & 7)) & 1;
	ua.enable = !state;
	ua.length = 1;

	for (i = ua.start + 1; i < IOPAGES * PAGE_SIZE * NBBY; i++) {
		if (state != ((iomap[i >> 3] >> (i & 7)) & 1))
			break;
		ua.length++;
	}
			
done:
	error = copyout(&ua, args, sizeof(struct i386_ioperm_args));
	return (error);
}

/*
 * Update the TLS entries for the process.  Used by assembly, do not staticize.
 *
 * Must be called from a critical section (else an interrupt thread preemption
 * may cause %gs to fault).  Normally called from the low level swtch.s code.
 *
 * MPSAFE
 */
void
set_user_TLS(void)
{
	struct thread *td = curthread;
	int i;
	int off = GTLS_START + mycpu->gd_cpuid * NGDT;
	for (i = 0; i < NGTLS; ++i)
		gdt[off + i].sd = td->td_tls.tls[i];
}

static
void
set_user_ldt_cpusync(void *arg)
{
	set_user_ldt(arg);
}

/*
 * Update the GDT entry pointing to the LDT to point to the LDT of the
 * current process.  Used by assembly, do not staticize.
 *
 * Must be called from a critical section (else an interrupt thread preemption
 * may cause %gs to fault).  Normally called from the low level swtch.s code.
 */   
void
set_user_ldt(struct pcb *pcb)
{
	struct pcb_ldt *pcb_ldt;

	if (pcb != curthread->td_pcb)
		return;

	pcb_ldt = pcb->pcb_ldt;
	gdt[mycpu->gd_cpuid * NGDT + GUSERLDT_SEL].sd = pcb_ldt->ldt_sd;
	lldt(GSEL(GUSERLDT_SEL, SEL_KPL));
	mdcpu->gd_currentldt = GSEL(GUSERLDT_SEL, SEL_KPL);
}

struct pcb_ldt *
user_ldt_alloc(struct pcb *pcb, int len)
{
	struct pcb_ldt *pcb_ldt, *new_ldt;

	new_ldt = kmalloc(sizeof(struct pcb_ldt), M_SUBPROC, M_WAITOK);

	new_ldt->ldt_len = len = NEW_MAX_LD(len);
	new_ldt->ldt_base = (caddr_t)kmem_alloc(&kernel_map,
					        len * sizeof(union descriptor));
	if (new_ldt->ldt_base == NULL) {
		kfree(new_ldt, M_SUBPROC);
		return NULL;
	}
	new_ldt->ldt_refcnt = 1;
	new_ldt->ldt_active = 0;

	gdt_segs[GUSERLDT_SEL].ssd_base = (unsigned)new_ldt->ldt_base;
	gdt_segs[GUSERLDT_SEL].ssd_limit = len * sizeof(union descriptor) - 1;
	ssdtosd(&gdt_segs[GUSERLDT_SEL], &new_ldt->ldt_sd);

	if ((pcb_ldt = pcb->pcb_ldt)) {
		if (len > pcb_ldt->ldt_len)
			len = pcb_ldt->ldt_len;
		bcopy(pcb_ldt->ldt_base, new_ldt->ldt_base,
			len * sizeof(union descriptor));
	} else {
		bcopy(ldt, new_ldt->ldt_base, sizeof(ldt));
	}
	return new_ldt;
}

void
user_ldt_free(struct pcb *pcb)
{
	struct pcb_ldt *pcb_ldt = pcb->pcb_ldt;

	if (pcb_ldt == NULL)
		return;

	crit_enter();
	if (pcb == curthread->td_pcb) {
		lldt(_default_ldt);
		mdcpu->gd_currentldt = _default_ldt;
	}
	pcb->pcb_ldt = NULL;
	crit_exit();

	if (--pcb_ldt->ldt_refcnt == 0) {
		kmem_free(&kernel_map, (vm_offset_t)pcb_ldt->ldt_base,
			  pcb_ldt->ldt_len * sizeof(union descriptor));
		kfree(pcb_ldt, M_SUBPROC);
	}
}

static int
ki386_get_ldt(struct lwp *lwp, char *args, int *res)
{
	int error = 0;
	struct pcb *pcb = lwp->lwp_thread->td_pcb;
	struct pcb_ldt *pcb_ldt = pcb->pcb_ldt;
	unsigned int nldt, num;
	union descriptor *lp;
	struct i386_ldt_args ua, *uap = &ua;

	if ((error = copyin(args, uap, sizeof(struct i386_ldt_args))) < 0)
		return(error);

#ifdef	DEBUG
	kprintf("ki386_get_ldt: start=%d num=%d descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif

	crit_enter();

	if (pcb_ldt) {
		nldt = (unsigned int)pcb_ldt->ldt_len;
		num = min(uap->num, nldt);
		lp = &((union descriptor *)(pcb_ldt->ldt_base))[uap->start];
	} else {
		nldt = (unsigned int)(NELEM(ldt));
		num = min(uap->num, nldt);
		lp = &ldt[uap->start];
	}

	/*
	 * note: uap->(args), num, and nldt are unsigned.  nldt and num
	 * are limited in scope, but uap->start can be anything.
	 */
	if (uap->start > nldt || uap->start + num > nldt) {
		crit_exit();
		return(EINVAL);
	}

	error = copyout(lp, uap->descs, num * sizeof(union descriptor));
	if (!error)
		*res = num;
	crit_exit();
	return(error);
}

static int
ki386_set_ldt(struct lwp *lp, char *args, int *res)
{
	int error = 0;
	int largest_ld;
	struct pcb *pcb = lp->lwp_thread->td_pcb;
	struct pcb_ldt *pcb_ldt = pcb->pcb_ldt;
	union descriptor *descs;
	int descs_size;
	struct i386_ldt_args ua, *uap = &ua;

	if ((error = copyin(args, uap, sizeof(struct i386_ldt_args))) < 0)
		return(error);

#ifdef	DEBUG
	kprintf("ki386_set_ldt: start=%d num=%d descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif

	/* verify range of descriptors to modify */
	if ((uap->start < 0) || (uap->start >= MAX_LD) || (uap->num < 0) ||
		(uap->num > MAX_LD))
	{
		return(EINVAL);
	}
	largest_ld = uap->start + uap->num - 1;
	if (largest_ld >= MAX_LD)
		return(EINVAL);

	/* allocate user ldt */
	if (!pcb_ldt || largest_ld >= pcb_ldt->ldt_len) {
		struct pcb_ldt *new_ldt = user_ldt_alloc(pcb, largest_ld);
		if (new_ldt == NULL)
			return ENOMEM;
		if (pcb_ldt) {
			pcb_ldt->ldt_sd = new_ldt->ldt_sd;
			kmem_free(&kernel_map, (vm_offset_t)pcb_ldt->ldt_base,
				  pcb_ldt->ldt_len * sizeof(union descriptor));
			pcb_ldt->ldt_base = new_ldt->ldt_base;
			pcb_ldt->ldt_len = new_ldt->ldt_len;
			kfree(new_ldt, M_SUBPROC);
		} else {
			pcb->pcb_ldt = pcb_ldt = new_ldt;
		}
		/*
		 * Since the LDT may be shared, we must signal other cpus to
		 * reload it.  XXX we need to track which cpus might be
		 * using the shared ldt and only signal those.
		 */
		lwkt_cpusync_simple(-1, set_user_ldt_cpusync, pcb);
	}

	descs_size = uap->num * sizeof(union descriptor);
	descs = (union descriptor *)kmem_alloc(&kernel_map, descs_size);
	if (descs == NULL)
		return (ENOMEM);
	error = copyin(&uap->descs[0], descs, descs_size);
	if (error) {
		kmem_free(&kernel_map, (vm_offset_t)descs, descs_size);
		return (error);
	}
	/* Check descriptors for access violations */
	error = check_descs(descs, uap->num);
	if (error) {
		kmem_free(&kernel_map, (vm_offset_t)descs, descs_size);
		return (error);
	}

	/*
	 * Fill in the actual ldt entries.  Since %fs or %gs might point to
	 * one of these entries a critical section is required to prevent an
	 * interrupt thread from preempting us, switch back, and faulting
	 * on the load of %fs due to a half-formed descriptor.
	 */
	crit_enter();
	bcopy(descs, 
		 &((union descriptor *)(pcb_ldt->ldt_base))[uap->start],
		uap->num * sizeof(union descriptor));
	*res = uap->start;

	crit_exit();
	kmem_free(&kernel_map, (vm_offset_t)descs, descs_size);
	return (0);
}

static int
check_descs(union descriptor *descs, int num)
{
	int i;

	/* Check descriptors for access violations */
	for (i = 0; i < num; i++) {
		union descriptor *dp;
		dp = &descs[i];

		switch (dp->sd.sd_type) {
		case SDT_SYSNULL:	/* system null */ 
			dp->sd.sd_p = 0;
			break;
		case SDT_SYS286TSS: /* system 286 TSS available */
		case SDT_SYSLDT:    /* system local descriptor table */
		case SDT_SYS286BSY: /* system 286 TSS busy */
		case SDT_SYSTASKGT: /* system task gate */
		case SDT_SYS286IGT: /* system 286 interrupt gate */
		case SDT_SYS286TGT: /* system 286 trap gate */
		case SDT_SYSNULL2:  /* undefined by Intel */ 
		case SDT_SYS386TSS: /* system 386 TSS available */
		case SDT_SYSNULL3:  /* undefined by Intel */
		case SDT_SYS386BSY: /* system 386 TSS busy */
		case SDT_SYSNULL4:  /* undefined by Intel */ 
		case SDT_SYS386IGT: /* system 386 interrupt gate */
		case SDT_SYS386TGT: /* system 386 trap gate */
		case SDT_SYS286CGT: /* system 286 call gate */ 
		case SDT_SYS386CGT: /* system 386 call gate */
			/* I can't think of any reason to allow a user proc
			 * to create a segment of these types.  They are
			 * for OS use only.
			 */
			return EACCES;

		/* memory segment types */
		case SDT_MEMEC:   /* memory execute only conforming */
		case SDT_MEMEAC:  /* memory execute only accessed conforming */
		case SDT_MEMERC:  /* memory execute read conforming */
		case SDT_MEMERAC: /* memory execute read accessed conforming */
			/* Must be "present" if executable and conforming. */
			if (dp->sd.sd_p == 0)
				return (EACCES);
			break;
		case SDT_MEMRO:   /* memory read only */
		case SDT_MEMROA:  /* memory read only accessed */
		case SDT_MEMRW:   /* memory read write */
		case SDT_MEMRWA:  /* memory read write accessed */
		case SDT_MEMROD:  /* memory read only expand dwn limit */
		case SDT_MEMRODA: /* memory read only expand dwn lim accessed */
		case SDT_MEMRWD:  /* memory read write expand dwn limit */  
		case SDT_MEMRWDA: /* memory read write expand dwn lim acessed */
		case SDT_MEME:    /* memory execute only */ 
		case SDT_MEMEA:   /* memory execute only accessed */
		case SDT_MEMER:   /* memory execute read */
		case SDT_MEMERA:  /* memory execute read accessed */
			break;
		default:
			return(EINVAL);
			/*NOTREACHED*/
		}

		/* Only user (ring-3) descriptors may be present. */
		if ((dp->sd.sd_p != 0) && (dp->sd.sd_dpl != SEL_UPL))
			return (EACCES);
	}
	return (0);
}

/*
 * Called when /dev/io is opened
 */
int
cpu_set_iopl(void)
{
	curthread->td_lwp->lwp_md.md_regs->tf_eflags |= PSL_IOPL;
	return(0);
}

/*
 * Called when /dev/io is closed
 */
int
cpu_clr_iopl(void)
{
	curthread->td_lwp->lwp_md.md_regs->tf_eflags &= ~PSL_IOPL;
	return(0);
}

