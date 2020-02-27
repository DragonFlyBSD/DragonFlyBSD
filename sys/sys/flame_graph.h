/*
 * SYS/FLAME_GRAPH.H
 *
 * Data structures for flame graph sampling
 */
#ifndef _SYS_FLAME_GRAPH_H_
#define	_SYS_FLAME_GRAPH_H_

#define FLAME_GRAPH_BASE_SYM	"_flame_graph_ary"

#define FLAME_GRAPH_FRAMES	32
#define FLAME_GRAPH_NENTRIES	256

struct flame_graph_entry {
	intptr_t	rips[FLAME_GRAPH_FRAMES];
};

struct flame_graph_pcpu {
	uint32_t nentries;
	uint32_t windex;
	struct flame_graph_entry *fge;	/* array of nentries */
	int	dummy[12];
} __cachealign;

#endif
