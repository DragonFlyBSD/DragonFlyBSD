/*-
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/ucontext.h,v 1.4 1999/10/11 20:33:17 luoqi Exp $
 */

#ifndef _SYS_UCONTEXT_H_
#define	_SYS_UCONTEXT_H_

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif
#ifndef _SYS_SIGNAL_H_
#include <sys/signal.h>
#endif
#include <machine/ucontext.h>

typedef struct __ucontext {
	/*
	 * Keep the order of the first two fields. Also,
	 * keep them the first two fields in the structure.
	 * This way we can have a union with struct
	 * sigcontext and ucontext_t. This allows us to
	 * support them both at the same time.
	 * note: the union is not defined, though.
	 */
	sigset_t	uc_sigmask;
	mcontext_t	uc_mcontext;

	struct __ucontext *uc_link;
	stack_t		uc_stack;
	int		__spare__[8];
} ucontext_t;

#ifndef _KERNEL
 
__BEGIN_DECLS
  
#if __BSD_VISIBLE || __POSIX_VISIBLE < 200809
int	getcontext(ucontext_t *) __returns_twice;
int	setcontext(const ucontext_t *) __dead2;
void	makecontext(ucontext_t *, void (*)(void), int, ...);
int	swapcontext(ucontext_t *, const ucontext_t *);
#endif
   
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_UCONTEXT_H_ */
