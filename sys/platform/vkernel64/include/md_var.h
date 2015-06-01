/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/platform/vkernel/include/md_var.h,v 1.24 2008/03/20 02:14:54 dillon Exp $
 */

#ifndef _MACHINE_MD_VAR_H_
#define _MACHINE_MD_VAR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_VKERNEL_H_
#include <sys/vkernel.h>
#endif
#ifndef _NET_ETHERNET_H_
#include <net/ethernet.h>
#endif

#define VKNETIF_MAX	16
#define VKDISK_MAX	16
#define	SERNOLEN        30

struct vknetif_info {
	int		tap_fd;
	int		tap_unit;
	in_addr_t	netif_addr;
	in_addr_t	netif_mask;
	u_char		*enaddr;
};

struct vkdisk_info {
        int fd;
        int unit;
	enum vkdisk_type { VKD_EMPTY, VKD_DISK, VKD_CD } type;
	char fname[MAXPATHLEN];
	char *serno;
};

extern	char	sigcode[];
extern	int	szsigcode;
extern	vpte_t	*KernelPTA;	/* NOTE: Offset for direct VA translation */
extern	vpte_t	*KernelPTD;
extern	vm_offset_t crashdumpmap;
extern  int	cpu_fxsr;

extern  char    cpu_vendor[];	/* XXX belongs in i386 */
extern  u_int   cpu_vendor_id;	/* XXX belongs in i386 */
extern  u_int   cpu_id;		/* XXX belongs in i386 */

extern struct vkdisk_info DiskInfo[VKDISK_MAX];
extern int	DiskNum;
extern int	MemImageFd;
extern struct vknetif_info NetifInfo[VKNETIF_MAX];
extern int	NetifNum;
extern int	_ucodesel, _udatasel;

extern int	via_feature_xcrypt;
extern int	via_feature_rng;

struct mdglobaldata;
struct __mcontext;

vpte_t *pmap_kpte(vm_offset_t va);
void cpu_gdinit (struct mdglobaldata *gd, int cpu);

void cpu_heavy_restore(void);	/* cannot be called from C */
void cpu_lwkt_restore(void);    /* cannot be called from C */
void cpu_idle_restore(void);    /* cannot be called from C */
void cpu_kthread_restore(void);	/* cannot be called from C */
thread_t cpu_exit_switch (struct thread *next);
void cpu_setregs (void);
void cpu_idle (void);
void cpu_mask_all_signals (void);
void cpu_unmask_all_signals (void);
void go_user (struct intrframe *frame);

void init_exceptions(void);
void init_kqueue(void);
void init_fpu(int supports_sse);
void kern_trap(struct trapframe *);
void user_trap(struct trapframe *);
void syscall2 (struct trapframe *);
void vcons_set_mode(int);
int npxdna(struct trapframe *);
void npxpush(struct __mcontext *mctx);
void npxpop(struct __mcontext *mctx);

void signalintr(int intr);

struct kqueue_info;
struct kqueue_info *kqueue_add(int, void (*)(void *, struct intrframe *), void *);
void kqueue_del(struct kqueue_info *);
struct kqueue_info *kqueue_add_timer(void (*func)(void *, struct intrframe *), void *data);
void kqueue_reload_timer(struct kqueue_info *info, int ms);


#endif
