/*
 * Tree search generalized from Knuth (6.2.2) Algorithm T just like
 * the AT&T man page says.
 *
 * The node_t structure is for internal use only, lint doesn't grok it.
 *
 * Written by reading the System V Interface Definition, not the code.
 *
 * Totally public domain.
 *
 * $NetBSD: tfind.c,v 1.2 1999/09/16 11:45:37 lukem Exp $
 * $FreeBSD: src/lib/libc/stdlib/tfind.c,v 1.1.2.1 2000/08/17 07:38:39 jhb Exp $
 * $DragonFly: src/lib/libc/stdlib/tfind.c,v 1.2 2003/06/17 04:26:46 dillon Exp $
 */

#include <sys/cdefs.h>

#include <assert.h>
#define _SEARCH_PRIVATE
#include <stdlib.h>
#include <search.h>

/* find a node, or return 0 */
void *
tfind(vkey, vrootp, compar)
	const void *vkey;		/* key to be found */
	void **vrootp;			/* address of the tree root */
	int (*compar) __P((const void *, const void *));
{
	node_t **rootp = (node_t **)vrootp;

	if (rootp == NULL)
		return NULL;

	while (*rootp != NULL) {		/* T1: */
		int r;

		if ((r = (*compar)(vkey, (*rootp)->key)) == 0)	/* T2: */
			return *rootp;		/* key found */
		rootp = (r < 0) ?
		    &(*rootp)->llink :		/* T3: follow left branch */
		    &(*rootp)->rlink;		/* T4: follow right branch */
	}
	return NULL;
}
