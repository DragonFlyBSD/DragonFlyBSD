/*-
 * Copyright (c) 1995 Bruce D. Evans.
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
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/i386/include/md_var.h,v 1.35.2.4 2003/01/22 20:14:53 jhb Exp $
 * $DragonFly: src/sys/platform/pc32/include/md_var.h,v 1.9 2003/07/23 02:30:18 dillon Exp $
 */

#ifndef _MACHINE_MD_VAR_H_
#define	_MACHINE_MD_VAR_H_

/*
 * Miscellaneous machine-dependent declarations.
 */

extern	int	Maxmem;
extern	u_int	atdevbase;	/* offset in virtual memory of ISA io mem */
extern	void	(*bcopy_vector) __P((const void *from, void *to, size_t len));
extern	int	busdma_swi_pending;
extern	int	(*copyin_vector) __P((const void *udaddr, void *kaddr,
				      size_t len));
extern	int	(*copyout_vector) __P((const void *kaddr, void *udaddr,
				       size_t len));
extern	u_int	cpu_exthigh;
extern	u_int	cpu_feature;
extern	u_int	cpu_fxsr;
extern	u_int	cpu_high;
extern	u_int	cpu_id;
extern	u_int	cpu_procinfo;
extern	char	cpu_vendor[];
extern	u_int	cyrix_did;
extern	char	kstack[];
#ifdef PC98
extern	int	need_pre_dma_flush;
extern	int	need_post_dma_flush;
#endif
extern	int	nfs_diskless_valid;
extern	void	(*ovbcopy_vector) __P((const void *from, void *to, size_t len));
extern	char	sigcode[];
extern	int	szsigcode, szosigcode;

typedef void alias_for_inthand_t __P((u_int cs, u_int ef, u_int esp, u_int ss));
struct	proc;
struct	reg;
struct	fpreg;
struct  dbreg;
struct  mdglobaldata;
struct  thread;

void	bcopyb __P((const void *from, void *to, size_t len));
void	busdma_swi __P((void));
void	cpu_gdinit __P((struct mdglobaldata *gd, int cpu));
void	cpu_halt __P((void));
void	cpu_idle __P((void));
void	cpu_reset __P((void));
void	cpu_setregs __P((void));
void	cpu_switch_load_gs __P((void)) __asm(__STRING(cpu_switch_load_gs));
void	cpu_heavy_restore __P((void));	/* cannot be called from C */
void	cpu_lwkt_restore __P((void));	/* cannot be called from C */
void	cpu_idle_restore __P((void));	/* cannot be called from C */
void	cpu_kthread_restore __P((void));/* cannot be called from C */
void	cpu_exit_idleptd __P((void)); 	/* disassociate proc MMU */
void	cpu_exit_switch __P((struct thread *next));
void	doreti_iret __P((void)) __asm(__STRING(doreti_iret));
void	doreti_iret_fault __P((void)) __asm(__STRING(doreti_iret_fault));
void	doreti_popl_ds __P((void)) __asm(__STRING(doreti_popl_ds));
void	doreti_popl_ds_fault __P((void)) __asm(__STRING(doreti_popl_ds_fault));
void	doreti_popl_es __P((void)) __asm(__STRING(doreti_popl_es));
void	doreti_popl_es_fault __P((void)) __asm(__STRING(doreti_popl_es_fault));
void	doreti_popl_fs __P((void)) __asm(__STRING(doreti_popl_fs));
void	doreti_popl_fs_fault __P((void)) __asm(__STRING(doreti_popl_fs_fault));
void	enable_sse __P((void));
int	fill_fpregs __P((struct proc *, struct fpreg *));
int	fill_regs __P((struct proc *p, struct reg *regs));
int	fill_dbregs __P((struct proc *p, struct dbreg *dbregs));
void	fillw __P((int /*u_short*/ pat, void *base, size_t cnt));
void	i486_bzero __P((volatile void *buf, size_t len));
void	i586_bzero __P((volatile void *buf, size_t len));
void	i586_bcopy __P((const void *from, void *to, size_t len));
int	i586_copyin __P((const void *udaddr, void *kaddr, size_t len));
int	i586_copyout __P((const void *kaddr, void *udaddr, size_t len));
void	i686_pagezero __P((void *addr));
void	init_AMD_Elan_sc520(void);
int	is_physical_memory __P((vm_offset_t addr));
u_long	kvtop __P((void *addr));
void	setidt __P((int idx, alias_for_inthand_t *func, int typ, int dpl,
		    int selec));
void	userconfig __P((void));
int     user_dbreg_trap __P((void));
int	vm_page_zero_idle __P((void));

#endif /* !_MACHINE_MD_VAR_H_ */
