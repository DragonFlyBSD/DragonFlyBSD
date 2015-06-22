#ifndef _CPU_TOPOLOGY_H_
#define _CPU_TOPOLOGY_H_

#ifdef _KERNEL

/* CPU TOPOLOGY DATA AND FUNCTIONS */
struct cpu_node {
	struct cpu_node * parent_node;
	struct cpu_node * child_node[MAXCPU];
	uint32_t child_no;
	cpumask_t members;
	uint8_t type;
	uint8_t compute_unit_id; /* AMD compute unit ID */
};
typedef struct cpu_node cpu_node_t;

extern int cpu_topology_levels_number;
extern cpu_node_t *root_cpu_node;

cpumask_t get_cpumask_from_level(int cpuid, uint8_t level_type);
cpu_node_t *get_cpu_node_by_cpuid(int cpuid);
const cpu_node_t *get_cpu_node_by_chipid(int chip_id);

#define LEVEL_NO 4

/* Level type for CPU siblings */
#define	PACKAGE_LEVEL	1
#define	CHIP_LEVEL	2
#define	CORE_LEVEL	3
#define	THREAD_LEVEL	4

#define CPUSET_FOREACH(cpu, mask)			\
	for ((cpu) = 0; (cpu) < ncpus; (cpu)++)		\
		if (CPUMASK_TESTBIT(mask, cpu))


#endif /* _KERNEL */
#endif /* _CPU_TOPOLOGY_H_ */
