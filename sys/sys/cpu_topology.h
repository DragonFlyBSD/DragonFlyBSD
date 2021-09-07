/*-
 * Copyright (c) 2021 The DragonFly Project.  All rights reserved.
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

#ifndef _SYS_CPU_TOPOLOGY_H_
#define _SYS_CPU_TOPOLOGY_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#include <sys/param.h>
#include <sys/cpumask.h>

/* CPU TOPOLOGY DATA AND FUNCTIONS */
struct cpu_node {
	struct cpu_node * parent_node;
	struct cpu_node * child_node[MAXCPU];
	uint32_t child_no;
	uint32_t unused01;
	cpumask_t members;
	uint8_t type;
	uint8_t compute_unit_id; /* AMD compute unit ID */
	uint8_t unused02;
	uint8_t unused03;
	long	phys_mem;	/* supplied from vm_numa_organize() */
};
typedef struct cpu_node cpu_node_t;

#define LEVEL_NO 4

/* Level type for CPU siblings */
#define	PACKAGE_LEVEL	1
#define	CHIP_LEVEL	2
#define	CORE_LEVEL	3
#define	THREAD_LEVEL	4

#define CPUSET_FOREACH(cpu, mask)			\
	for ((cpu) = 0; (cpu) < ncpus; (cpu)++)		\
		if (CPUMASK_TESTBIT(mask, cpu))

#endif

#if defined(_KERNEL)

extern int cpu_topology_levels_number;
extern int cpu_topology_ht_ids;
extern int cpu_topology_core_ids;
extern int cpu_topology_phys_ids;
extern cpu_node_t *root_cpu_node;

cpumask_t get_cpumask_from_level(int cpuid, uint8_t level_type);
cpu_node_t *get_cpu_node_by_cpuid(int cpuid);
const cpu_node_t *get_cpu_node_by_chipid(int chip_id);
long get_highest_node_memory(void);
int get_cpu_ht_id(int cpuid);
int get_cpu_core_id(int cpuid);
int get_cpu_phys_id(int cpuid);

#endif /* _KERNEL */
#endif /* _SYS_CPU_TOPOLOGY_H_ */
