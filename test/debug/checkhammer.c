/*
 * checkhammer.c
 *
 * checkhammer blockmapdump btreedump
 */

#include <sys/types.h>
#include <sys/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct rbmap_tree;
struct rbmap;

static void parseBlockMap(FILE *fp);
static void parseBTree(FILE *fp);
static void dumpResults(void);
static int rbmap_cmp(struct rbmap *, struct rbmap *);

typedef u_int64_t hammer_off_t;
typedef struct rbmap *rbmap_t;

RB_HEAD(rbmap_tree, rbmap);
RB_PROTOTYPE2(rbmap_tree, rbmap, rbentry, rbmap_cmp, hammer_off_t);

struct rbmap {
	RB_ENTRY(rbmap) rbentry;
	hammer_off_t	base;
	long		app;
	long		free;
	long		bytes;
	int		zone;
};

RB_GENERATE2(rbmap_tree, rbmap, rbentry, rbmap_cmp, hammer_off_t, base);

struct rbmap_tree rbroot;

static
int
rbmap_cmp(struct rbmap *rb1, struct rbmap *rb2)
{
	if (rb1->base < rb2->base)
		return(-1);
	if (rb1->base > rb2->base)
		return(1);
	return(0);
}

int
main(int ac, char **av)
{
	FILE *fp;

	if (ac != 3) {
		fprintf(stderr, "checkhammer blockmapdump btreedump\n");
		exit(1);
	}
	if ((fp = fopen(av[1], "r")) == NULL) {
		fprintf(stderr, "Unable to open %s\n", av[1]);
		exit(1);
	}

	RB_INIT(&rbroot);
	parseBlockMap(fp);
	fclose(fp);
	if ((fp = fopen(av[2], "r")) == NULL) {
		fprintf(stderr, "Unable to open %s\n", av[1]);
		exit(1);
	}
	parseBTree(fp);
	fclose(fp);

	dumpResults();
	return(0);
}

static void
parseBlockMap(FILE *fp)
{
	char buf[1024];
	rbmap_t map;
	int zone;
	long long base;
	long long app;
	long long free;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "        4%llx zone=%d app=%lld free=%lld",
			   &base, &zone, &app, &free) != 4)
			continue;
		if (RB_LOOKUP(rbmap_tree, &rbroot, (hammer_off_t)base))
			continue;
		map = malloc(sizeof(*map));
		map->base = (hammer_off_t)base;
		map->app = (long)app;
		map->free = (long)free;
		map->zone = zone;
		map->bytes = 0;
		RB_INSERT(rbmap_tree, &rbroot, map);
	}
}

static void
parseBTree(FILE *fp)
{
	char buf[1024];
	rbmap_t map;
	long long base;
	long long bytes;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "     NODE 8%llx", &base) == 1) {
			base &= 0x0FFFFFFFFF800000LLU;
			map = RB_LOOKUP(rbmap_tree, &rbroot, base);
			if (map == NULL) {
				printf("(not in blockmap): %s", buf);
				continue;
			}
			map->bytes += 4096;
		}
		if (sscanf(buf, "                 dataoff=%llx/%lld",
			   &base, &bytes) == 2) {
			base &= 0x0FFFFFFFFF800000LLU;
			map = RB_LOOKUP(rbmap_tree, &rbroot, base);
			if (map == NULL) {
				printf("(not in blockmap): %s", buf);
				continue;
			}
			map->bytes += (bytes + 15) & ~15;
		}
	}
}

static void
dumpResults(void)
{
	rbmap_t map;
	hammer_off_t bfree;

	printf("mismatches: (blockmap, actual)\n");
	RB_FOREACH(map, rbmap_tree, &rbroot) {
		bfree = 8192 * 1024 - (int64_t)map->bytes;

		/*
		 * Ignore matches
		 */
		if (map->free == bfree)
			continue;

		/*
		 * If the block is completely allocated but our calculations
		 * show nobody is referencing it it is probably an undo,
		 * blockmap, or unavailable reserved area.
		 */
		if (map->free == 0 && bfree == 8192 * 1024) {
			if (map->zone == 3 || map->zone == 4 ||
			    map->zone == 15)
				continue;
		}

		printf(" bmap %016jx %jd %jd\n",
			map->base,
			(intmax_t)(int64_t)map->free,
			(intmax_t)(int64_t)bfree);
	}
}
