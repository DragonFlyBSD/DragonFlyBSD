/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <sys/cpu_topology.h>

#include <machine/smp.h>

#ifdef SMP

#ifndef NAPICID
#define NAPICID 256
#endif

#define INDENT_BUF_SIZE LEVEL_NO*3
#define INVALID_ID -1

/* Per-cpu sysctl nodes and info */
struct per_cpu_sysctl_info {
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	char cpu_name[32];
	int physical_id;
	int core_id;
	char physical_siblings[8*MAXCPU];
	char core_siblings[8*MAXCPU];
};
typedef struct per_cpu_sysctl_info per_cpu_sysctl_info_t;

static cpu_node_t cpu_topology_nodes[MAXCPU];	/* Memory for topology */
static cpu_node_t *cpu_root_node;		/* Root node pointer */

static struct sysctl_ctx_list cpu_topology_sysctl_ctx;
static struct sysctl_oid *cpu_topology_sysctl_tree;
static char cpu_topology_members[8*MAXCPU];
static per_cpu_sysctl_info_t pcpu_sysctl[MAXCPU];

int cpu_topology_levels_number = 1;

/* Get the next valid apicid starting
 * from current apicid (curr_apicid
 */
static int
get_next_valid_apicid(int curr_apicid)
{
	int next_apicid = curr_apicid;
	do {
		next_apicid++;
	}
	while(get_cpuid_from_apicid(next_apicid) == -1 &&
	   next_apicid < NAPICID);
	if (next_apicid == NAPICID) {
		kprintf("Warning: No next valid APICID found. Returning -1\n");
		return -1;
	}
	return next_apicid;
}

/* Generic topology tree. The parameters have the following meaning:
 * - children_no_per_level : the number of children on each level
 * - level_types : the type of the level (THREAD, CORE, CHIP, etc)
 * - cur_level : the current level of the tree
 * - node : the current node
 * - last_free_node : the last free node in the global array.
 * - cpuid : basicly this are the ids of the leafs
 */ 
static void
build_topology_tree(int *children_no_per_level,
   uint8_t *level_types,
   int cur_level, 
   cpu_node_t *node,
   cpu_node_t **last_free_node,
   int *apicid)
{
	int i;

	node->child_no = children_no_per_level[cur_level];
	node->type = level_types[cur_level];
	node->members = 0;

	if (node->child_no == 0) {
		node->child_node = NULL;
		*apicid = get_next_valid_apicid(*apicid);
		node->members = CPUMASK(get_cpuid_from_apicid(*apicid));
		return;
	}

	node->child_node = *last_free_node;
	(*last_free_node) += node->child_no;
	
	for (i = 0; i < node->child_no; i++) {

		node->child_node[i].parent_node = node;

		build_topology_tree(children_no_per_level,
		    level_types,
		    cur_level + 1,
		    &(node->child_node[i]),
		    last_free_node,
		    apicid);

		node->members |= node->child_node[i].members;
	}
}

/* Build CPU topology. The detection is made by comparing the
 * chip, core and logical IDs of each CPU with the IDs of the 
 * BSP. When we found a match, at that level the CPUs are siblings.
 */
static cpu_node_t *
build_cpu_topology(void)
{
	detect_cpu_topology();
	int i;
	int BSPID = 0;
	int threads_per_core = 0;
	int cores_per_chip = 0;
	int chips_per_package = 0;
	int children_no_per_level[LEVEL_NO];
	uint8_t level_types[LEVEL_NO];
	int apicid = -1;

	cpu_node_t *root = &cpu_topology_nodes[0];
	cpu_node_t *last_free_node = root + 1;

	/* Assume that the topology is uniform.
	 * Find the number of siblings within chip
	 * and witin core to build up the topology
	 */
	for (i = 0; i < ncpus; i++) {

		cpumask_t mask = CPUMASK(i);

		if ((mask & smp_active_mask) == 0)
			continue;

		if (get_chip_ID(BSPID) == get_chip_ID(i))
			cores_per_chip++;
		else
			continue;

		if (get_core_number_within_chip(BSPID) ==
		    get_core_number_within_chip(i))
			threads_per_core++;
	}

	cores_per_chip /= threads_per_core;
	chips_per_package = ncpus / (cores_per_chip * threads_per_core);
	
	if (bootverbose)
		kprintf("CPU Topology: cores_per_chip: %d; threads_per_core: %d; chips_per_package: %d;\n",
		    cores_per_chip, threads_per_core, chips_per_package);

	if (threads_per_core > 1) { /* HT available - 4 levels */

		children_no_per_level[0] = chips_per_package;
		children_no_per_level[1] = cores_per_chip;
		children_no_per_level[2] = threads_per_core;
		children_no_per_level[3] = 0;

		level_types[0] = PACKAGE_LEVEL;
		level_types[1] = CHIP_LEVEL;
		level_types[2] = CORE_LEVEL;
		level_types[3] = THREAD_LEVEL;
	
		build_topology_tree(children_no_per_level,
		    level_types,
		    0,
		    root,
		    &last_free_node,
		    &apicid);

		cpu_topology_levels_number = 4;

	} else if (cores_per_chip > 1) { /* No HT available - 3 levels */

		children_no_per_level[0] = chips_per_package;
		children_no_per_level[1] = cores_per_chip;
		children_no_per_level[2] = 0;

		level_types[0] = PACKAGE_LEVEL;
		level_types[1] = CHIP_LEVEL;
		level_types[2] = CORE_LEVEL;
	
		build_topology_tree(children_no_per_level,
		    level_types,
		    0,
		    root,
		    &last_free_node,
		    &apicid);

		cpu_topology_levels_number = 3;

	} else { /* No HT and no Multi-Core - 2 levels */

		children_no_per_level[0] = chips_per_package;
		children_no_per_level[1] = 0;

		level_types[0] = PACKAGE_LEVEL;
		level_types[1] = CHIP_LEVEL;
	
		build_topology_tree(children_no_per_level,
		    level_types,
		    0,
		    root,
		    &last_free_node,
		    &apicid);

		cpu_topology_levels_number = 2;

	}

	return root;
}

/* Recursive function helper to print the CPU topology tree */
static void
print_cpu_topology_tree_sysctl_helper(cpu_node_t *node,
    struct sbuf *sb,
    char * buf,
    int buf_len,
    int last)
{
	int i;
	int bsr_member;

	sbuf_bcat(sb, buf, buf_len);
	if (last) {
		sbuf_printf(sb, "\\-");
		buf[buf_len] = ' ';buf_len++;
		buf[buf_len] = ' ';buf_len++;
	} else {
		sbuf_printf(sb, "|-");
		buf[buf_len] = '|';buf_len++;
		buf[buf_len] = ' ';buf_len++;
	}
	
	bsr_member = BSRCPUMASK(node->members);

	if (node->type == PACKAGE_LEVEL) {
		sbuf_printf(sb,"PACKAGE MEMBERS: ");
	} else if (node->type == CHIP_LEVEL) {
		sbuf_printf(sb,"CHIP ID %d: ",
			get_chip_ID(bsr_member));
	} else if (node->type == CORE_LEVEL) {
		sbuf_printf(sb,"CORE ID %d: ",
			get_core_number_within_chip(bsr_member));
	} else if (node->type == THREAD_LEVEL) {
		sbuf_printf(sb,"THREAD ID %d: ",
			get_logical_CPU_number_within_core(bsr_member));
	} else {
		sbuf_printf(sb,"UNKNOWN: ");
	}
	CPUSET_FOREACH(i, node->members) {
		sbuf_printf(sb,"cpu%d ", i);
	}	
	
	sbuf_printf(sb,"\n");

	for (i = 0; i < node->child_no; i++) {
		print_cpu_topology_tree_sysctl_helper(&(node->child_node[i]),
		    sb, buf, buf_len, i == (node->child_no -1));
	}
}

/* SYSCTL PROCEDURE for printing the CPU Topology tree */
static int
print_cpu_topology_tree_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *sb;
	int ret;
	char buf[INDENT_BUF_SIZE];

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	sb = sbuf_new(NULL, NULL, 500, SBUF_AUTOEXTEND);
	if (sb == NULL) {
		return (ENOMEM);
	}
	sbuf_printf(sb,"\n");
	print_cpu_topology_tree_sysctl_helper(cpu_root_node, sb, buf, 0, 1);

	sbuf_finish(sb);

	ret = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));

	sbuf_delete(sb);

	return ret;
}

/* SYSCTL PROCEDURE for printing the CPU Topology level description */
static int
print_cpu_topology_level_description_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *sb;
	int ret;

	sb = sbuf_new(NULL, NULL, 500, SBUF_AUTOEXTEND);
	if (sb == NULL)
		return (ENOMEM);

	if (cpu_topology_levels_number == 4) /* HT available */
		sbuf_printf(sb, "0 - thread; 1 - core; 2 - socket; 3 - anything");
	else if (cpu_topology_levels_number == 3) /* No HT available */
		sbuf_printf(sb, "0 - core; 1 - socket; 2 - anything");
	else if (cpu_topology_levels_number == 2) /* No HT and no Multi-Core */
		sbuf_printf(sb, "0 - socket; 1 - anything");
	else
		sbuf_printf(sb, "Unknown");

	sbuf_finish(sb);

	ret = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));

	sbuf_delete(sb);

	return ret;	
}

/* Find a cpu_node_t by a mask */
static cpu_node_t *
get_cpu_node_by_cpumask(cpu_node_t * node,
			cpumask_t mask) {

	cpu_node_t * found = NULL;
	int i;

	if (node->members == mask) {
		return node;
	}

	for (i = 0; i < node->child_no; i++) {
		found = get_cpu_node_by_cpumask(&(node->child_node[i]), mask);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

cpu_node_t *
get_cpu_node_by_cpuid(int cpuid) {
	cpumask_t mask = CPUMASK(cpuid);

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	return get_cpu_node_by_cpumask(cpu_root_node, mask);
}

/* Get the mask of siblings for level_type of a cpuid */
cpumask_t
get_cpumask_from_level(int cpuid,
			uint8_t level_type)
{
	cpu_node_t * node;
	cpumask_t mask = CPUMASK(cpuid);

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	node = get_cpu_node_by_cpumask(cpu_root_node, mask);

	if (node == NULL) {
		return 0;
	}

	while (node != NULL) {
		if (node->type == level_type) {
			return node->members;
		}
		node = node->parent_node;
	}

	return 0;
}

/* init pcpu_sysctl structure info */
static void
init_pcpu_topology_sysctl(void)
{
	int cpu;
	int i;
	cpumask_t mask;
	struct sbuf sb;

	for (i = 0; i < ncpus; i++) {

		sbuf_new(&sb, pcpu_sysctl[i].cpu_name,
		    sizeof(pcpu_sysctl[i].cpu_name), SBUF_FIXEDLEN);
		sbuf_printf(&sb,"cpu%d", i);
		sbuf_finish(&sb);


		/* Get physical siblings */
		mask = get_cpumask_from_level(i, CHIP_LEVEL);
		if (mask == 0) {
			pcpu_sysctl[i].physical_id = INVALID_ID;
			continue;
		}

		sbuf_new(&sb, pcpu_sysctl[i].physical_siblings,
		    sizeof(pcpu_sysctl[i].physical_siblings), SBUF_FIXEDLEN);
		CPUSET_FOREACH(cpu, mask) {
			sbuf_printf(&sb,"cpu%d ", cpu);
		}
		sbuf_trim(&sb);
		sbuf_finish(&sb);

		pcpu_sysctl[i].physical_id = get_chip_ID(i); 

		/* Get core siblings */
		mask = get_cpumask_from_level(i, CORE_LEVEL);
		if (mask == 0) {
			pcpu_sysctl[i].core_id = INVALID_ID;
			continue;
		}

		sbuf_new(&sb, pcpu_sysctl[i].core_siblings,
		    sizeof(pcpu_sysctl[i].core_siblings), SBUF_FIXEDLEN);
		CPUSET_FOREACH(cpu, mask) {
			sbuf_printf(&sb,"cpu%d ", cpu);
		}
		sbuf_trim(&sb);
		sbuf_finish(&sb);

		pcpu_sysctl[i].core_id = get_core_number_within_chip(i);

	}
}

/* Build SYSCTL structure for revealing
 * the CPU Topology to user-space.
 */
static void
build_sysctl_cpu_topology(void)
{
	int i;
	struct sbuf sb;
	
	/* SYSCTL new leaf for "cpu_topology" */
	sysctl_ctx_init(&cpu_topology_sysctl_ctx);
	cpu_topology_sysctl_tree = SYSCTL_ADD_NODE(&cpu_topology_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw),
	    OID_AUTO,
	    "cpu_topology",
	    CTLFLAG_RD, 0, "");

	/* SYSCTL cpu_topology "tree" entry */
	SYSCTL_ADD_PROC(&cpu_topology_sysctl_ctx,
	    SYSCTL_CHILDREN(cpu_topology_sysctl_tree),
	    OID_AUTO, "tree", CTLTYPE_STRING | CTLFLAG_RD,
	    NULL, 0, print_cpu_topology_tree_sysctl, "A",
	    "Tree print of CPU topology");

	/* SYSCTL cpu_topology "level_description" entry */
	SYSCTL_ADD_PROC(&cpu_topology_sysctl_ctx,
	    SYSCTL_CHILDREN(cpu_topology_sysctl_tree),
	    OID_AUTO, "level_description", CTLTYPE_STRING | CTLFLAG_RD,
	    NULL, 0, print_cpu_topology_level_description_sysctl, "A",
	    "Level description of CPU topology");

	/* SYSCTL cpu_topology "members" entry */
	sbuf_new(&sb, cpu_topology_members,
	    sizeof(cpu_topology_members), SBUF_FIXEDLEN);
	CPUSET_FOREACH(i, cpu_root_node->members) {
		sbuf_printf(&sb,"cpu%d ", i);
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	SYSCTL_ADD_STRING(&cpu_topology_sysctl_ctx,
	    SYSCTL_CHILDREN(cpu_topology_sysctl_tree),
	    OID_AUTO, "members", CTLFLAG_RD,
	    cpu_topology_members, 0,
	    "Members of the CPU Topology");

	/* SYSCTL per_cpu info */
	for (i = 0; i < ncpus; i++) {
		/* New leaf : hw.cpu_topology.cpux */
		sysctl_ctx_init(&pcpu_sysctl[i].sysctl_ctx); 
		pcpu_sysctl[i].sysctl_tree = SYSCTL_ADD_NODE(&pcpu_sysctl[i].sysctl_ctx,
		    SYSCTL_CHILDREN(cpu_topology_sysctl_tree),
		    OID_AUTO,
		    pcpu_sysctl[i].cpu_name,
		    CTLFLAG_RD, 0, "");

		/* Check if the physical_id found is valid */
		if (pcpu_sysctl[i].physical_id == INVALID_ID) {
			continue;
		}

		/* Add physical id info */
		SYSCTL_ADD_INT(&pcpu_sysctl[i].sysctl_ctx,
		    SYSCTL_CHILDREN(pcpu_sysctl[i].sysctl_tree),
		    OID_AUTO, "physical_id", CTLFLAG_RD,
		    &pcpu_sysctl[i].physical_id, 0,
		    "Physical ID");

		/* Add physical siblings */
		SYSCTL_ADD_STRING(&pcpu_sysctl[i].sysctl_ctx,
		    SYSCTL_CHILDREN(pcpu_sysctl[i].sysctl_tree),
		    OID_AUTO, "physical_siblings", CTLFLAG_RD,
		    pcpu_sysctl[i].physical_siblings, 0,
		    "Physical siblings");

		/* Check if the core_id found is valid */
		if (pcpu_sysctl[i].core_id == INVALID_ID) {
			continue;
		}

		/* Add core id info */
		SYSCTL_ADD_INT(&pcpu_sysctl[i].sysctl_ctx,
		    SYSCTL_CHILDREN(pcpu_sysctl[i].sysctl_tree),
		    OID_AUTO, "core_id", CTLFLAG_RD,
		    &pcpu_sysctl[i].core_id, 0,
		    "Core ID");
		
		/*Add core siblings */
		SYSCTL_ADD_STRING(&pcpu_sysctl[i].sysctl_ctx,
		    SYSCTL_CHILDREN(pcpu_sysctl[i].sysctl_tree),
		    OID_AUTO, "core_siblings", CTLFLAG_RD,
		    pcpu_sysctl[i].core_siblings, 0,
		    "Core siblings");
	}
}

/* Build the CPU Topology and SYSCTL Topology tree */
static void
init_cpu_topology(void)
{
	cpu_root_node = build_cpu_topology();

	init_pcpu_topology_sysctl();
	build_sysctl_cpu_topology();
}
SYSINIT(cpu_topology, SI_BOOT2_CPU_TOPOLOGY, SI_ORDER_FIRST,
    init_cpu_topology, NULL)
#endif
