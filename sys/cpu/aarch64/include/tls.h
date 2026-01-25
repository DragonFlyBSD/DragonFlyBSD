/*
 * Copyright (c) 2005,2008,2020 The DragonFly Project.
 * Copyright (c) 2026 The DragonFly Project.
 * All rights reserved.
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
 */

#ifndef _CPU_TLS_H_
#define _CPU_TLS_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TLS_H_
#include <sys/tls.h>
#endif

/*
 * NOTE: the tcb_{self,dtv,pthread,errno) fields must be declared
 * in the structure in the specified order.
 */
struct tls_tcb {
	struct tls_tcb *tcb_self;
	void *tcb_dtv;
	void *tcb_pthread;
	int *tcb_errno_p;
	void *tcb_segstack;
	void *tcb_unused[3];
};

struct tls_dtv {
	uintptr_t dtv_generation;
	uintptr_t dtv_max_index;
	void *dtv_offset[1];
};

#define RTLD_TCB_HAS_SELF_POINTER
#define RTLD_STATIC_TLS_ALIGN		16
#define RTLD_STATIC_TLS_ALIGN_MASK	(RTLD_STATIC_TLS_ALIGN - 1)
#define RTLD_STATIC_TLS_EXTRA_MIN	2048
#define RTLD_STATIC_TLS_EXTRA_MAX	(1ULL * 1024 * 1024 * 1024)
#define RTLD_STATIC_TLS_EXTRA_DEFAULT	6144
#define RTLD_STATIC_TLS_VARIANT_II

/*
 * ARM64 TLS save structure for vkernel support (stub).
 * ARM64 uses TPIDR_EL0 for TLS, no descriptor tables needed.
 */
struct savetls {
	void	*tls_base;	/* TLS base pointer (from TPIDR_EL0) */
};

#endif /* !_CPU_TLS_H_ */
