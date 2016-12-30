/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)kvm.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD: src/lib/libkvm/kvm.h,v 1.11 1999/08/27 23:44:50 peter Exp $
 * $DragonFly: src/lib/libkvm/kvm.h,v 1.7 2007/02/01 10:33:25 corecode Exp $
 */

#ifndef _KVM_H_
#define	_KVM_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>
#include <nlist.h>

/* Default version symbol. */
#define	VRS_SYM		"_version"
#define	VRS_KEY		"VERSION"

typedef struct __kvm kvm_t;

struct kinfo_file;
struct kinfo_proc;
struct proc;
struct nchstats;

struct kvm_swap {
	char	ksw_devname[32];
	u_int	ksw_used;
	u_int	ksw_total;
	int	ksw_flags;
	u_int	ksw_reserved1;
	u_int	ksw_reserved2;
};

#define SWIF_DUMP_TREE	0x0001
#define SWIF_DEV_PREFIX	0x0002

__BEGIN_DECLS
void kvm_nch_cpuagg(struct nchstats *, struct nchstats *, int);
int	  kvm_close (kvm_t *);
char	**kvm_getargv (kvm_t *, const struct kinfo_proc *, int);
char	**kvm_getenvv (kvm_t *, const struct kinfo_proc *, int);
char	 *kvm_geterr (kvm_t *);
struct kinfo_file *
	  kvm_getfiles (kvm_t *, int, int, int *);
int	  kvm_getloadavg (kvm_t *, double [], int);
struct kinfo_proc *
	  kvm_getprocs (kvm_t *, int, int, int *);
int	  kvm_getswapinfo (kvm_t *, struct kvm_swap *, int, int);
int	  kvm_nlist (kvm_t *, struct nlist *);
kvm_t	 *kvm_open
	    (const char *, const char *, const char *, int, const char *);
kvm_t	 *kvm_openfiles
	    (const char *, const char *, const char *, int, char *);
__ssize_t	  kvm_read (kvm_t *, unsigned long, void *, __size_t);
char	 *kvm_readstr(kvm_t *, u_long, char *, size_t *);
__ssize_t	  kvm_uread
	    (kvm_t *, pid_t, unsigned long, char *, __size_t);
__ssize_t	  kvm_write (kvm_t *, unsigned long, const void *, __size_t);
__END_DECLS

#endif /* !_KVM_H_ */
