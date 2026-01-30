/*-
 * Copyright (c) 2026 The DragonFly Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#ifndef _LINUX_DRAGONFLY_COMPAT_H_
#define _LINUX_DRAGONFLY_COMPAT_H_

#ifdef __DragonFly__

/*
 * DragonFly compatibility layer for LinuxKPI
 * 
 * This header provides DragonFly equivalents for FreeBSD kernel APIs
 * that LinuxKPI depends on but which differ or don't exist in DragonFly.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/lock.h>

/* Missing sys/smp.h - DragonFly uses different SMP APIs */
#ifndef _SYS_SMP_H_
#define _SYS_SMP_H_

#include <sys/cpu.h>

/* DragonFly uses ncpus instead of mp_ncpus */
#define mp_ncpus ncpus

/* CPU identification */
#define CPU_FOREACH(cpu) for ((cpu) = 0; (cpu) < ncpus; (cpu)++)

/* smp_processor_id equivalent */
#define smp_processor_id curcpu

#endif /* _SYS_SMP_H_ */

/* Missing sys/taskqueue.h - DragonFly uses taskqueue differently */
#ifndef _SYS_TASKQUEUE_H_
#define _SYS_TASKQUEUE_H_

#include <sys/taskqueue.h>

/* FreeBSD taskqueue types mapped to DragonFly */
#define taskqueue_thread_enqueue taskqueue_enqueue

#endif /* _SYS_TASKQUEUE_H_ */

/* Missing sys/kdb.h - DragonFly has different debugger interface */
#ifndef _SYS_KDB_H_
#define _SYS_KDB_H_

/* Stub for now - DragonFly doesn't use kdb in the same way */
#define KDB_WHY_UNSET 0
#define KDB_WHY_BOOT 1

static __inline int
kdb_active(void)
{
    return 0; /* DragonFly doesn't have kdb_active equivalent */
}

#endif /* _SYS_KDB_H_ */

/* Missing sys/mutex2.h - FreeBSD internal mutex details */
#ifndef _SYS_MUTEX2_H_
#define _SYS_MUTEX2_H_

/* DragonFly doesn't have mutex2.h - use regular mutex.h */
#include <sys/mutex.h>

#endif /* _SYS_MUTEX2_H_ */

/* Missing sys/lock2.h - FreeBSD internal lock details */
#ifndef _SYS_LOCK2_H_
#define _SYS_LOCK2_H_

/* DragonFly doesn't have lock2.h - use regular lock.h */
#include <sys/lock.h>

#endif /* _SYS_LOCK2_H_ */

/* Missing opt_stack.h - generated header in FreeBSD */
#ifndef _OPT_STACK_H_
#define _OPT_STACK_H_

/* Stack protector options - default to enabled */
#ifndef STACK_PROTECTOR
#define STACK_PROTECTOR 1
#endif

#endif /* _OPT_STACK_H_ */

/* Additional compatibility macros */

/* FreeBSD curthread vs DragonFly curthread - same name, should work */
/* FreeBSD curproc vs DragonFly curproc - same name, should work */

/* CPU time ticks - DragonFly uses different fields */
#define ticks ticks

/* Process time tracking */
#define p_timetick td_sticks  /* DragonFly uses td_sticks in thread */

/* Missing vm/uma.h components if needed */
#ifdef _VM_UMA_H_
/* DragonFly has uma in sys/uma.h */
#endif

#endif /* __DragonFly__ */

#endif /* _LINUX_DRAGONFLY_COMPAT_H_ */
