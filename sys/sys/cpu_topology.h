#ifndef _CPU_TOPOLOGY_H_
#define _CPU_TOPOLOGY_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

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
#endif /* _CPU_TOPOLOGY_H_ */
