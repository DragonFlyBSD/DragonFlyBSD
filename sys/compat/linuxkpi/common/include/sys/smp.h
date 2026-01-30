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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_SMP_H_
#define _SYS_SMP_H_

/* DragonFly SMP compatibility for LinuxKPI */

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpuset.h>

/* 
 * NOTE: sys/cpu.h does NOT exist in DragonFly
 * CPU count (ncpus) is declared in sys/systm.h and sys/kernel.h
 */

/* DragonFly uses ncpus instead of mp_ncpus */
#define mp_ncpus ncpus
#define mp_maxid (ncpus - 1)

/* CPU identification */
#define CPU_FOREACH(cpu) for ((cpu) = 0; (cpu) < ncpus; (cpu)++)
#define CPU_FOREACH_ISSET(cpu, cpuset) CPU_FOREACH(cpu)

/* smp_processor_id equivalent - use curcpu inline function */
#define smp_processor_id() curcpu

/* SMP-related macros */
#define smp_rendezvous_cpus(cpus, setup_func, action_func, teardown_func, arg) \
    do { if (setup_func) setup_func(arg); \
         if (action_func) action_func(arg); \
         if (teardown_func) teardown_func(arg); \
    } while(0)

#define smp_rendezvous(setup, action, teardown, arg) \
    smp_rendezvous_cpus(NULL, setup, action, teardown, arg)

/* Topology stubs */
#ifndef MAXCPU
#define MAXCPU 256
#endif

/* CPU topology - DragonFly uses different structure */
static __inline int
cpu_number(void)
{
    return curcpu;
}

static __inline int
cpu_topology_start(int cpu)
{
    return 0;
}

static __inline int
cpu_topology_next(int cpu, int prev)
{
    return -1;
}

static __inline int
cpu_topology_level(int cpu)
{
    return 0;
}

static __inline int
smp_cpu_id(int cpu)
{
    return cpu;
}

/* Topology sibling masks - simplified */
typedef cpuset_t cpu_topo_mask_t;

static __inline cpuset_t *
cpu_sibling_mask(int cpu)
{
    static cpuset_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    return &mask;
}

static __inline cpuset_t *
cpu_core_mask(int cpu)
{
    return cpu_sibling_mask(cpu);
}

static __inline cpuset_t *
cpu_l3_mask(int cpu)
{
    return cpu_sibling_mask(cpu);
}

static __inline cpuset_t *
cpu_l2_mask(int cpu)
{
    return cpu_sibling_mask(cpu);
}

static __inline cpuset_t *
cpu_l1_mask(int cpu)
{
    return cpu_sibling_mask(cpu);
}

/* Thread ID - simplified */
static __inline int
cpu_thread(int cpu)
{
    return 0;
}

/* Core ID - simplified */
static __inline int
cpu_core(int cpu)
{
    return cpu;
}

/* Package ID - simplified */
static __inline int
cpu_package(int cpu)
{
    return 0;
}

/* Physical CPU ID */
static __inline int
cpu_pcpu(int cpu)
{
    return cpu;
}

#endif /* _SYS_SMP_H_ */
