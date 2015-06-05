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
static void sbuf_print_cpuset(struct sbuf *sb, cpumask_t *mask);

int cpu_topology_levels_number = 1;
cpu_node_t *root_cpu_node;

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
	CPUMASK_ASSZERO(node->members);
	node->compute_unit_id = -1;

	if (node->child_no == 0) {
		*apicid = get_next_valid_apicid(*apicid);
		CPUMASK_ASSBIT(node->members, get_cpuid_from_apicid(*apicid));
		return;
	}

	if (node->parent_node == NULL)
		root_cpu_node = node;
	
	for (i = 0; i < node->child_no; i++) {
		node->child_node[i] = *last_free_node;
		(*last_free_node)++;

		node->child_node[i]->parent_node = node;

		build_topology_tree(children_no_per_level,
		    level_types,
		    cur_level + 1,
		    node->child_node[i],
		    last_free_node,
		    apicid);

		CPUMASK_ORMASK(node->members, node->child_node[i]->members);
	}
}

#if defined(__x86_64__) && !defined(_KERNEL_VIRTUAL)
static void
migrate_elements(cpu_node_t **a, int n, int pos)
{
	int i;

	for (i = pos; i < n - 1 ; i++) {
		a[i] = a[i+1];
	}
	a[i] = NULL;
}
#endif

/* Build CPU topology. The detection is made by comparing the
 * chip, core and logical IDs of each CPU with the IDs of the 
 * BSP. When we found a match, at that level the CPUs are siblings.
 */
static void
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
		cpumask_t mask;

		CPUMASK_ASSBIT(mask, i);

		if (CPUMASK_TESTMASK(mask, smp_active_mask) == 0)
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

	cpu_root_node = root;


#if defined(__x86_64__) && !defined(_KERNEL_VIRTUAL)
	if (fix_amd_topology() == 0) {
		int visited[MAXCPU], i, j, pos, cpuid;
		cpu_node_t *leaf, *parent;

		bzero(visited, MAXCPU * sizeof(int));

		for (i = 0; i < ncpus; i++) {
			if (visited[i] == 0) {
				pos = 0;
				visited[i] = 1;
				leaf = get_cpu_node_by_cpuid(i);

				if (leaf->type == CORE_LEVEL) {
					parent = leaf->parent_node;

					last_free_node->child_node[0] = leaf;
					last_free_node->child_no = 1;
					last_free_node->members = leaf->members;
					last_free_node->compute_unit_id = leaf->compute_unit_id;
					last_free_node->parent_node = parent;
					last_free_node->type = CORE_LEVEL;


					for (j = 0; j < parent->child_no; j++) {
						if (parent->child_node[j] != leaf) {

							cpuid = BSFCPUMASK(parent->child_node[j]->members);
							if (visited[cpuid] == 0 &&
							    parent->child_node[j]->compute_unit_id == leaf->compute_unit_id) {

								last_free_node->child_node[last_free_node->child_no] = parent->child_node[j];
								last_free_node->child_no++;
								CPUMASK_ORMASK(last_free_node->members, parent->child_node[j]->members);

								parent->child_node[j]->type = THREAD_LEVEL;
								parent->child_node[j]->parent_node = last_free_node;
								visited[cpuid] = 1;

								migrate_elements(parent->child_node, parent->child_no, j);
								parent->child_no--;
								j--;
							}
						} else {
							pos = j;
						}
					}
					if (last_free_node->child_no > 1) {
						parent->child_node[pos] = last_free_node;
						leaf->type = THREAD_LEVEL;
						leaf->parent_node = last_free_node;
						last_free_node++;
					}
				}
			}
		}
	}
#endif
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
		if (node->compute_unit_id != (uint8_t)-1) {
			sbuf_printf(sb,"Compute Unit ID %d: ",
				node->compute_unit_id);
		} else {
			sbuf_printf(sb,"CORE ID %d: ",
				get_core_number_within_chip(bsr_member));
		}
	} else if (node->type == THREAD_LEVEL) {
		if (node->compute_unit_id != (uint8_t)-1) {
			sbuf_printf(sb,"CORE ID %d: ",
				get_core_number_within_chip(bsr_member));
		} else {
			sbuf_printf(sb,"THREAD ID %d: ",
				get_logical_CPU_number_within_core(bsr_member));
		}
	} else {
		sbuf_printf(sb,"UNKNOWN: ");
	}
	sbuf_print_cpuset(sb, &node->members);
	sbuf_printf(sb,"\n");

	for (i = 0; i < node->child_no; i++) {
		print_cpu_topology_tree_sysctl_helper(node->child_node[i],
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

	if (CPUMASK_CMPMASKEQ(node->members, mask))
		return node;

	for (i = 0; i < node->child_no; i++) {
		found = get_cpu_node_by_cpumask(node->child_node[i], mask);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

cpu_node_t *
get_cpu_node_by_cpuid(int cpuid) {
	cpumask_t mask;

	CPUMASK_ASSBIT(mask, cpuid);

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	return get_cpu_node_by_cpumask(cpu_root_node, mask);
}

/* Get the mask of siblings for level_type of a cpuid */
cpumask_t
get_cpumask_from_level(int cpuid,
			uint8_t level_type)
{
	cpu_node_t * node;
	cpumask_t mask;

	CPUMASK_ASSBIT(mask, cpuid);

	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));

	node = get_cpu_node_by_cpumask(cpu_root_node, mask);

	if (node == NULL) {
		CPUMASK_ASSZERO(mask);
		return mask;
	}

	while (node != NULL) {
		if (node->type == level_type) {
			return node->members;
		}
		node = node->parent_node;
	}
	CPUMASK_ASSZERO(mask);

	return mask;
}

static const cpu_node_t *
get_cpu_node_by_chipid2(const cpu_node_t *node, int chip_id)
{
	int cpuid;

	if (node->type != CHIP_LEVEL) {
		const cpu_node_t *ret = NULL;
		int i;

		for (i = 0; i < node->child_no; ++i) {
			ret = get_cpu_node_by_chipid2(node->child_node[i],
			    chip_id);
			if (ret != NULL)
				break;
		}
		return ret;
	}

	cpuid = BSRCPUMASK(node->members);
	if (get_chip_ID(cpuid) == chip_id)
		return node;
	return NULL;
}

const cpu_node_t *
get_cpu_node_by_chipid(int chip_id)
{
	KASSERT(cpu_root_node != NULL, ("cpu_root_node isn't initialized"));
	return get_cpu_node_by_chipid2(cpu_root_node, chip_id);
}

/* init pcpu_sysctl structure info */
static void
init_pcpu_topology_sysctl(void)
{
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
		if (CPUMASK_TESTZERO(mask)) {
			pcpu_sysctl[i].physical_id = INVALID_ID;
			continue;
		}

		sbuf_new(&sb, pcpu_sysctl[i].physical_siblings,
		    sizeof(pcpu_sysctl[i].physical_siblings), SBUF_FIXEDLEN);
		sbuf_print_cpuset(&sb, &mask);
		sbuf_trim(&sb);
		sbuf_finish(&sb);

		pcpu_sysctl[i].physical_id = get_chip_ID(i); 

		/* Get core siblings */
		mask = get_cpumask_from_level(i, CORE_LEVEL);
		if (CPUMASK_TESTZERO(mask)) {
			pcpu_sysctl[i].core_id = INVALID_ID;
			continue;
		}

		sbuf_new(&sb, pcpu_sysctl[i].core_siblings,
		    sizeof(pcpu_sysctl[i].core_siblings), SBUF_FIXEDLEN);
		sbuf_print_cpuset(&sb, &mask);
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
	sbuf_print_cpuset(&sb, &cpu_root_node->members);
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

static
void
sbuf_print_cpuset(struct sbuf *sb, cpumask_t *mask)
{
	int i;
	int b = -1;
	int e = -1;
	int more = 0;

	sbuf_printf(sb, "cpus(");
	CPUSET_FOREACH(i, *mask) {
		if (b < 0) {
			b = i;
			e = b + 1;
			continue;
		}
		if (e == i) {
			++e;
			continue;
		}
		if (more)
			sbuf_printf(sb, ", ");
		if (b == e - 1) {
			sbuf_printf(sb, "%d", b);
		} else {
			sbuf_printf(sb, "%d-%d", b, e - 1);
		}
		more = 1;
		b = i;
		e = b + 1;
	}
	if (more)
		sbuf_printf(sb, ", ");
	if (b >= 0) {
		if (b == e - 1) {
			sbuf_printf(sb, "%d", b);
		} else {
			sbuf_printf(sb, "%d-%d", b, e - 1);
		}
	}
	sbuf_printf(sb, ") ");
}

/* Build the CPU Topology and SYSCTL Topology tree */
static void
init_cpu_topology(void)
{
	build_cpu_topology();

	init_pcpu_topology_sysctl();
	build_sysctl_cpu_topology();
}
SYSINIT(cpu_topology, SI_BOOT2_CPU_TOPOLOGY, SI_ORDER_FIRST,
    init_cpu_topology, NULL);
