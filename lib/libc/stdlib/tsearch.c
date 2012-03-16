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
 * $NetBSD: tsearch.c,v 1.3 1999/09/16 11:45:37 lukem Exp $
 * $FreeBSD: src/lib/libc/stdlib/tsearch.c,v 1.4 2003/01/05 02:43:18 tjr Exp $
 */

#define _SEARCH_PRIVATE
#include <search.h>
#include <stdlib.h>

/*
 * find or insert datum into search tree
 *
 * Parameters:
 *	vkey:	key to be located
 *	vrootp:	address of tree root
 */

void *
tsearch(const void *vkey, void **vrootp,
	int (*compar)(const void *, const void *))
{
	node_t *q;
	node_t **rootp = (node_t **)vrootp;

	if (rootp == NULL)
		return NULL;

	while (*rootp != NULL) {	/* Knuth's T1: */
		int r;

		if ((r = (*compar)(vkey, (*rootp)->key)) == 0)	/* T2: */
			return *rootp;		/* we found it! */

		rootp = (r < 0) ?
		    &(*rootp)->llink :		/* T3: follow left branch */
		    &(*rootp)->rlink;		/* T4: follow right branch */
	}

	q = malloc(sizeof(node_t));		/* T5: key not found */
	if (q != NULL) {			/* make new node */
		*rootp = q;			/* link new node to old */
		/* LINTED const castaway ok */
		q->key = __DECONST(void *, vkey); /* initialize new node */
		q->llink = q->rlink = NULL;
	}
	return q;
}
