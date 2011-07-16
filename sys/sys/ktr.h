/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 */
/*
 * Generic Kernel trace buffer support.  
 *
 */

#ifndef _SYS_KTR_H_
#define	_SYS_KTR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_SYSCTL_H_
#include <sys/sysctl.h>
#endif

#ifdef _KERNEL
#include "opt_ktr.h"
#endif

/*
 * Conveniently auto-enable KTRs when particular KTRs are specified
 * in kernel options.  This way we can get logging during boot.
 * However, if KTR_ALL is optioned, just disable everything by default.
 * The user can enable individual masks via sysctl.
 */
#if defined(KTR_ALL)
#define KTR_AUTO_ENABLE		0
#else
#define KTR_ALL			0
#define KTR_AUTO_ENABLE		-1
#endif

#define	KTR_BUFSIZE		192
#define KTR_VERSION		4

#define KTR_VERSION_WITH_FREQ	3
#define KTR_VERSION_KTR_CPU	4

#ifndef LOCORE

struct ktr_info {
	const char *kf_name;	/* human-interpreted subsystem name */
	int32_t *kf_master_enable; /* the master enable variable */
	const char *kf_format;	/* data format */
	int kf_data_size;	/* relevance of the data buffer */
};

struct ktr_entry {
	u_int64_t ktr_timestamp;
	struct ktr_info *ktr_info;
	const	char *ktr_file;
	void	*ktr_caller1;
	void	*ktr_caller2;
	int32_t	ktr_line;
	int32_t ktr_unused;
	int32_t	ktr_data[KTR_BUFSIZE / sizeof(int32_t)];
};

struct ktr_cpu_core {
	struct ktr_entry *ktr_buf;
	int		 ktr_idx;
};

struct ktr_cpu {
	struct ktr_cpu_core core;
	char pad[__VM_CACHELINE_ALIGN(sizeof(struct ktr_cpu_core))];
};

#ifdef _KERNEL

void	ktr_log(struct ktr_info *info, const char *file, int line, ...);
void    cpu_ktr_caller(struct ktr_entry *ktr);

#endif

/*
 * Take advantage of constant integer optimizations by the compiler
 * to optimize-out disabled code at compile-time.  If KTR_ENABLE_<name>
 * is 0, the compiler avoids generating all related code when KTR is enabled.
 */

#ifdef KTR

SYSCTL_DECL(_debug_ktr);

#define KTR_INFO_MASTER(master)						    \
	    SYSCTL_NODE(_debug_ktr, OID_AUTO, master, CTLFLAG_RW, 0, "");   \
	    int ktr_ ## master ## _enable = KTR_AUTO_ENABLE;		    \
	    SYSCTL_INT(_debug_ktr, OID_AUTO, master ## _enable, CTLFLAG_RW, \
		&ktr_ ## master ## _enable, 0,				    \
		"Bit mask to control " __XSTRING(master) "'s event logging")

#define KTR_INFO_MASTER_EXTERN(master)					\
	    SYSCTL_DECL(_debug_ktr_ ## master);				\
	    extern int ktr_ ## master ## _enable			\

/*
 * This creates a read-only sysctl so the user knows what the mask
 * definitions are and a number of static const int's which are used
 * by the compiler to optimize the trace logging at compile-time and
 * run-time.
 */
#define KTR_INFO(compile, master, name, maskbit, format, datasize)	\
	    static const int ktr_ ## master ## _ ## name ## _mask =	\
		1 << maskbit;						\
	    static const int ktr_ ## master ## _ ## name ## _enable =	\
		compile;						\
	    static int ktr_ ## master ## _ ## name ## _mask_ro =	\
		1 << maskbit;						\
	    SYSCTL_INT(_debug_ktr_ ## master, OID_AUTO, name ## _mask,	\
		CTLFLAG_RD, &ktr_ ## master ## _ ## name ## _mask_ro,	\
		0, "Value of the " __XSTRING(name) " event in " __XSTRING(master) "'s mask"); \
	    static struct ktr_info ktr_info_ ## master ## _ ## name = { \
		#master "_" #name,					\
		&ktr_ ## master ## _enable,				\
		format,							\
		datasize }

#define KTR_LOG(name, args...)						\
	    if (ktr_ ## name ## _enable &&				\
	      (ktr_ ## name ## _mask & *ktr_info_ ## name .kf_master_enable)) \
	    ktr_log(&ktr_info_ ## name, __FILE__, __LINE__, ##args)

#else

#define KTR_INFO_MASTER(master)						\
	    const static int ktr_ ## master ## _enable = 0

#define KTR_INFO_MASTER_EXTERN(master)					\
	    const static int ktr_ ## master ## _enable

#define KTR_INFO(compile, master, name, maskbit, format, datasize)	\
	    static const int ktr_ ## master ## _ ## name ## _mask =	\
		1 << maskbit

#define KTR_LOG(info, args...)

#endif

#endif /* !LOCORE */
#endif /* !_SYS_KTR_H_ */
