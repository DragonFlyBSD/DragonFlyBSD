/*
 * Copyright (c) 1988, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)radix.c	8.4 (Berkeley) 11/2/94
 * $FreeBSD: src/sys/net/radix.c,v 1.20.2.3 2002/04/28 05:40:25 suz Exp $
 */

/*
 * Routines to build and maintain radix trees for routing lookups.
 */
#include <sys/param.h>
#ifdef	_KERNEL
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/domain.h>
#include <sys/globaldata.h>
#include <sys/thread.h>
#else
#include <stdlib.h>
#endif
#include <sys/syslog.h>
#include <net/radix.h>

/*
 * The arguments to the radix functions are really counted byte arrays with
 * the length in the first byte.  struct sockaddr's fit this type structurally.
 */
#define clen(c)	(*(u_char *)(c))

static int rn_walktree_from(struct radix_node_head *h, char *a, char *m,
			    walktree_f_t *f, void *w);
static int rn_walktree(struct radix_node_head *, walktree_f_t *, void *);

static struct radix_node
    *rn_insert(char *, struct radix_node_head *, boolean_t *,
	       struct radix_node [2]),
    *rn_newpair(char *, int, struct radix_node[2]),
    *rn_search(const char *, struct radix_node *),
    *rn_search_m(const char *, struct radix_node *, const char *);

static struct radix_mask *rn_mkfreelist[MAXCPU];
static struct radix_node_head *mask_rnheads[MAXCPU];

static char rn_zeros[RN_MAXKEYLEN];
static char rn_ones[RN_MAXKEYLEN] = RN_MAXKEYONES;

static int rn_lexobetter(char *m, char *n);
static struct radix_mask *
    rn_new_radix_mask(struct radix_node *tt, struct radix_mask *nextmask);
static boolean_t
    rn_satisfies_leaf(char *trial, struct radix_node *leaf, int skip);

static __inline struct radix_mask *
MKGet(struct radix_mask **l)
{
	struct radix_mask *m;

	if (*l != NULL) {
		m = *l;
		*l = m->rm_next;
	} else {
		R_Malloc(m, struct radix_mask *, sizeof *m);
	}
	return m;
}

static __inline void
MKFree(struct radix_mask **l, struct radix_mask *m)
{
	m->rm_next = *l;
	*l = m;
}

/*
 * The data structure for the keys is a radix tree with one way
 * branching removed.  The index rn_bit at an internal node n represents a bit
 * position to be tested.  The tree is arranged so that all descendants
 * of a node n have keys whose bits all agree up to position rn_bit - 1.
 * (We say the index of n is rn_bit.)
 *
 * There is at least one descendant which has a one bit at position rn_bit,
 * and at least one with a zero there.
 *
 * A route is determined by a pair of key and mask.  We require that the
 * bit-wise logical and of the key and mask to be the key.
 * We define the index of a route to associated with the mask to be
 * the first bit number in the mask where 0 occurs (with bit number 0
 * representing the highest order bit).
 *
 * We say a mask is normal if every bit is 0, past the index of the mask.
 * If a node n has a descendant (k, m) with index(m) == index(n) == rn_bit,
 * and m is a normal mask, then the route applies to every descendant of n.
 * If the index(m) < rn_bit, this implies the trailing last few bits of k
 * before bit b are all 0, (and hence consequently true of every descendant
 * of n), so the route applies to all descendants of the node as well.
 *
 * Similar logic shows that a non-normal mask m such that
 * index(m) <= index(n) could potentially apply to many children of n.
 * Thus, for each non-host route, we attach its mask to a list at an internal
 * node as high in the tree as we can go.
 *
 * The present version of the code makes use of normal routes in short-
 * circuiting an explict mask and compare operation when testing whether
 * a key satisfies a normal route, and also in remembering the unique leaf
 * that governs a subtree.
 */

static struct radix_node *
rn_search(const char *v, struct radix_node *head)
{
	struct radix_node *x;

	x = head;
	while (x->rn_bit >= 0) {
		if (x->rn_bmask & v[x->rn_offset])
			x = x->rn_right;
		else
			x = x->rn_left;
	}
	return (x);
}

static struct radix_node *
rn_search_m(const char *v, struct radix_node *head, const char *m)
{
	struct radix_node *x;

	for (x = head; x->rn_bit >= 0;) {
		if ((x->rn_bmask & m[x->rn_offset]) &&
		    (x->rn_bmask & v[x->rn_offset]))
			x = x->rn_right;
		else
			x = x->rn_left;
	}
	return x;
}

boolean_t
rn_refines(char *m, char *n)
{
	char *lim, *lim2;
	int longer = clen(n++) - clen(m++);
	boolean_t masks_are_equal = TRUE;

	lim2 = lim = n + clen(n);
	if (longer > 0)
		lim -= longer;
	while (n < lim) {
		if (*n & ~(*m))
			return FALSE;
		if (*n++ != *m++)
			masks_are_equal = FALSE;
	}
	while (n < lim2)
		if (*n++)
			return FALSE;
	if (masks_are_equal && (longer < 0))
		for (lim2 = m - longer; m < lim2; )
			if (*m++)
				return TRUE;
	return (!masks_are_equal);
}

struct radix_node *
rn_lookup(char *key, char *mask, struct radix_node_head *head)
{
	struct radix_node *x;
	char *netmask = NULL;

	if (mask != NULL) {
		x = rn_addmask(mask, TRUE, head->rnh_treetop->rn_offset,
			       head->rnh_maskhead);
		if (x == NULL)
			return (NULL);
		netmask = x->rn_key;
	}
	x = rn_match(key, head);
	if (x != NULL && netmask != NULL) {
		while (x != NULL && x->rn_mask != netmask)
			x = x->rn_dupedkey;
	}
	return x;
}

static boolean_t
rn_satisfies_leaf(char *trial, struct radix_node *leaf, int skip)
{
	char *cp = trial, *cp2 = leaf->rn_key, *cp3 = leaf->rn_mask;
	char *cplim;
	int length = min(clen(cp), clen(cp2));

	if (cp3 == NULL)
		cp3 = rn_ones;
	else
		length = min(length, clen(cp3));
	cplim = cp + length;
	cp3 += skip;
	cp2 += skip;
	for (cp += skip; cp < cplim; cp++, cp2++, cp3++)
		if ((*cp ^ *cp2) & *cp3)
			return FALSE;
	return TRUE;
}

struct radix_node *
rn_match(char *key, struct radix_node_head *head)
{
	struct radix_node *t, *x;
	char *cp = key, *cp2;
	char *cplim;
	struct radix_node *saved_t, *top = head->rnh_treetop;
	int off = top->rn_offset, klen, matched_off;
	int test, b, rn_bit;

	t = rn_search(key, top);
	/*
	 * See if we match exactly as a host destination
	 * or at least learn how many bits match, for normal mask finesse.
	 *
	 * It doesn't hurt us to limit how many bytes to check
	 * to the length of the mask, since if it matches we had a genuine
	 * match and the leaf we have is the most specific one anyway;
	 * if it didn't match with a shorter length it would fail
	 * with a long one.  This wins big for class B&C netmasks which
	 * are probably the most common case...
	 */
	if (t->rn_mask != NULL)
		klen = clen(t->rn_mask);
	else
		klen = clen(key);
	cp += off; cp2 = t->rn_key + off; cplim = key + klen;
	for (; cp < cplim; cp++, cp2++)
		if (*cp != *cp2)
			goto on1;
	/*
	 * This extra grot is in case we are explicitly asked
	 * to look up the default.  Ugh!
	 *
	 * Never return the root node itself, it seems to cause a
	 * lot of confusion.
	 */
	if (t->rn_flags & RNF_ROOT)
		t = t->rn_dupedkey;
	return t;
on1:
	test = (*cp ^ *cp2) & 0xff; /* find first bit that differs */
	for (b = 7; (test >>= 1) > 0;)
		b--;
	matched_off = cp - key;
	b += matched_off << 3;
	rn_bit = -1 - b;
	/*
	 * If there is a host route in a duped-key chain, it will be first.
	 */
	if ((saved_t = t)->rn_mask == NULL)
		t = t->rn_dupedkey;
	for (; t; t = t->rn_dupedkey) {
		/*
		 * Even if we don't match exactly as a host,
		 * we may match if the leaf we wound up at is
		 * a route to a net.
		 */
		if (t->rn_flags & RNF_NORMAL) {
			if (rn_bit <= t->rn_bit)
				return t;
		} else if (rn_satisfies_leaf(key, t, matched_off))
				return t;
	}
	t = saved_t;
	/* start searching up the tree */
	do {
		struct radix_mask *m;

		t = t->rn_parent;
		/*
		 * If non-contiguous masks ever become important
		 * we can restore the masking and open coding of
		 * the search and satisfaction test and put the
		 * calculation of "off" back before the "do".
		 */
		m = t->rn_mklist;
		while (m != NULL) {
			if (m->rm_flags & RNF_NORMAL) {
				if (rn_bit <= m->rm_bit)
					return (m->rm_leaf);
			} else {
				off = min(t->rn_offset, matched_off);
				x = rn_search_m(key, t, m->rm_mask);
				while (x != NULL && x->rn_mask != m->rm_mask)
					x = x->rn_dupedkey;
				if (x && rn_satisfies_leaf(key, x, off))
					return x;
			}
			m = m->rm_next;
		}
	} while (t != top);
	return NULL;
}

#ifdef RN_DEBUG
int rn_nodenum;
struct radix_node *rn_clist;
int rn_saveinfo;
boolean_t rn_debug =  TRUE;
#endif

static struct radix_node *
rn_newpair(char *key, int indexbit, struct radix_node nodes[2])
{
	struct radix_node *leaf = &nodes[0], *interior = &nodes[1];

	interior->rn_bit = indexbit;
	interior->rn_bmask = 0x80 >> (indexbit & 0x7);
	interior->rn_offset = indexbit >> 3;
	interior->rn_left = leaf;
	interior->rn_mklist = NULL;

	leaf->rn_bit = -1;
	leaf->rn_key = key;
	leaf->rn_parent = interior;
	leaf->rn_flags = interior->rn_flags = RNF_ACTIVE;
	leaf->rn_mklist = NULL;

#ifdef RN_DEBUG
	leaf->rn_info = rn_nodenum++;
	interior->rn_info = rn_nodenum++;
	leaf->rn_twin = interior;
	leaf->rn_ybro = rn_clist;
	rn_clist = leaf;
#endif
	return interior;
}

static struct radix_node *
rn_insert(char *key, struct radix_node_head *head, boolean_t *dupentry,
	  struct radix_node nodes[2])
{
	struct radix_node *top = head->rnh_treetop;
	int head_off = top->rn_offset, klen = clen(key);
	struct radix_node *t = rn_search(key, top);
	char *cp = key + head_off;
	int b;
	struct radix_node *tt;

	/*
	 * Find first bit at which the key and t->rn_key differ
	 */
    {
	char *cp2 = t->rn_key + head_off;
	int cmp_res;
	char *cplim = key + klen;

	while (cp < cplim)
		if (*cp2++ != *cp++)
			goto on1;
	*dupentry = TRUE;
	return t;
on1:
	*dupentry = FALSE;
	cmp_res = (cp[-1] ^ cp2[-1]) & 0xff;
	for (b = (cp - key) << 3; cmp_res; b--)
		cmp_res >>= 1;
    }
    {
	struct radix_node *p, *x = top;

	cp = key;
	do {
		p = x;
		if (cp[x->rn_offset] & x->rn_bmask)
			x = x->rn_right;
		else
			x = x->rn_left;
	} while (b > (unsigned) x->rn_bit);
				/* x->rn_bit < b && x->rn_bit >= 0 */
#ifdef RN_DEBUG
	if (rn_debug)
		log(LOG_DEBUG, "rn_insert: Going In:\n"), traverse(p);
#endif
	t = rn_newpair(key, b, nodes);
	tt = t->rn_left;
	if ((cp[p->rn_offset] & p->rn_bmask) == 0)
		p->rn_left = t;
	else
		p->rn_right = t;
	x->rn_parent = t;
	t->rn_parent = p; /* frees x, p as temp vars below */
	if ((cp[t->rn_offset] & t->rn_bmask) == 0) {
		t->rn_right = x;
	} else {
		t->rn_right = tt;
		t->rn_left = x;
	}
#ifdef RN_DEBUG
	if (rn_debug)
		log(LOG_DEBUG, "rn_insert: Coming Out:\n"), traverse(p);
#endif
    }
	return (tt);
}

struct radix_node *
rn_addmask(char *netmask, boolean_t search, int skip,
	   struct radix_node_head *mask_rnh)
{
	struct radix_node *x, *saved_x;
	char *cp, *cplim;
	int b = 0, mlen, m0, j;
	boolean_t maskduplicated, isnormal;
	char *addmask_key;

	if ((mlen = clen(netmask)) > RN_MAXKEYLEN)
		mlen = RN_MAXKEYLEN;
	if (skip == 0)
		skip = 1;
	if (mlen <= skip)
		return (mask_rnh->rnh_nodes);
	R_Malloc(addmask_key, char *, RN_MAXKEYLEN);
	if (addmask_key == NULL)
		return NULL;
	if (skip > 1)
		bcopy(rn_ones + 1, addmask_key + 1, skip - 1);
	if ((m0 = mlen) > skip)
		bcopy(netmask + skip, addmask_key + skip, mlen - skip);
	/*
	 * Trim trailing zeroes.
	 */
	for (cp = addmask_key + mlen; (cp > addmask_key) && cp[-1] == 0;)
		cp--;
	mlen = cp - addmask_key;
	if (mlen <= skip) {
		if (m0 >= mask_rnh->rnh_last_zeroed)
			mask_rnh->rnh_last_zeroed = mlen;
		Free(addmask_key);
		return (mask_rnh->rnh_nodes);
	}
	if (m0 < mask_rnh->rnh_last_zeroed)
		bzero(addmask_key + m0, mask_rnh->rnh_last_zeroed - m0);
	*addmask_key = mask_rnh->rnh_last_zeroed = mlen;
	x = rn_search(addmask_key, mask_rnh->rnh_treetop);
	if (x->rn_key == NULL) {
		kprintf("WARNING: radix_node->rn_key is NULL rn=%p\n", x);
		print_backtrace(-1);
		x = NULL;
	} else if (bcmp(addmask_key, x->rn_key, mlen) != 0) {
		x = NULL;
	}
	if (x != NULL || search)
		goto out;
	R_Malloc(x, struct radix_node *, RN_MAXKEYLEN + 2 * (sizeof *x));
	if ((saved_x = x) == NULL)
		goto out;
	bzero(x, RN_MAXKEYLEN + 2 * (sizeof *x));
	netmask = cp = (char *)(x + 2);
	bcopy(addmask_key, cp, mlen);
	x = rn_insert(cp, mask_rnh, &maskduplicated, x);
	if (maskduplicated) {
		log(LOG_ERR, "rn_addmask: mask impossibly already in tree");
		Free(saved_x);
		goto out;
	}
	/*
	 * Calculate index of mask, and check for normalcy.
	 */
	isnormal = TRUE;
	cplim = netmask + mlen;
	for (cp = netmask + skip; cp < cplim && clen(cp) == 0xff;)
		cp++;
	if (cp != cplim) {
		static const char normal_chars[] = {
			0, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, -1
		};

		for (j = 0x80; (j & *cp) != 0; j >>= 1)
			b++;
		if (*cp != normal_chars[b] || cp != (cplim - 1))
			isnormal = FALSE;
	}
	b += (cp - netmask) << 3;
	x->rn_bit = -1 - b;
	if (isnormal)
		x->rn_flags |= RNF_NORMAL;
out:
	Free(addmask_key);
	return (x);
}

/* XXX: arbitrary ordering for non-contiguous masks */
static boolean_t
rn_lexobetter(char *mp, char *np)
{
	char *lim;

	if ((unsigned) *mp > (unsigned) *np)
		return TRUE;/* not really, but need to check longer one first */
	if (*mp == *np)
		for (lim = mp + clen(mp); mp < lim;)
			if (*mp++ > *np++)
				return TRUE;
	return FALSE;
}

static struct radix_mask *
rn_new_radix_mask(struct radix_node *tt, struct radix_mask *nextmask)
{
	struct radix_mask *m;

	m = MKGet(&rn_mkfreelist[mycpuid]);
	if (m == NULL) {
		log(LOG_ERR, "Mask for route not entered\n");
		return (NULL);
	}
	bzero(m, sizeof *m);
	m->rm_bit = tt->rn_bit;
	m->rm_flags = tt->rn_flags;
	if (tt->rn_flags & RNF_NORMAL)
		m->rm_leaf = tt;
	else
		m->rm_mask = tt->rn_mask;
	m->rm_next = nextmask;
	tt->rn_mklist = m;
	return m;
}

struct radix_node *
rn_addroute(char *key, char *netmask, struct radix_node_head *head,
	    struct radix_node treenodes[2])
{
	struct radix_node *t, *x = NULL, *tt;
	struct radix_node *saved_tt, *top = head->rnh_treetop;
	short b = 0, b_leaf = 0;
	boolean_t keyduplicated;
	char *mmask;
	struct radix_mask *m, **mp;

	/*
	 * In dealing with non-contiguous masks, there may be
	 * many different routes which have the same mask.
	 * We will find it useful to have a unique pointer to
	 * the mask to speed avoiding duplicate references at
	 * nodes and possibly save time in calculating indices.
	 */
	if (netmask != NULL)  {
		if ((x = rn_addmask(netmask, FALSE, top->rn_offset,
				    head->rnh_maskhead)) == NULL)
			return (NULL);
		b_leaf = x->rn_bit;
		b = -1 - x->rn_bit;
		netmask = x->rn_key;
	}
	/*
	 * Deal with duplicated keys: attach node to previous instance
	 */
	saved_tt = tt = rn_insert(key, head, &keyduplicated, treenodes);
	if (keyduplicated) {
		for (t = tt; tt; t = tt, tt = tt->rn_dupedkey) {
			if (tt->rn_mask == netmask)
				return (NULL);
			if (netmask == NULL ||
			    (tt->rn_mask &&
			     ((b_leaf < tt->rn_bit) /* index(netmask) > node */
			      || rn_refines(netmask, tt->rn_mask)
			      || rn_lexobetter(netmask, tt->rn_mask))))
				break;
		}
		/*
		 * If the mask is not duplicated, we wouldn't
		 * find it among possible duplicate key entries
		 * anyway, so the above test doesn't hurt.
		 *
		 * We sort the masks for a duplicated key the same way as
		 * in a masklist -- most specific to least specific.
		 * This may require the unfortunate nuisance of relocating
		 * the head of the list.
		 */
		if (tt == saved_tt) {
			struct	radix_node *xx = x;
			/* link in at head of list */
			(tt = treenodes)->rn_dupedkey = t;
			tt->rn_flags = t->rn_flags;
			tt->rn_parent = x = t->rn_parent;
			t->rn_parent = tt;			/* parent */
			if (x->rn_left == t)
				x->rn_left = tt;
			else
				x->rn_right = tt;
			saved_tt = tt; x = xx;
		} else {
			(tt = treenodes)->rn_dupedkey = t->rn_dupedkey;
			t->rn_dupedkey = tt;
			tt->rn_parent = t;			/* parent */
			if (tt->rn_dupedkey != NULL)		/* parent */
				tt->rn_dupedkey->rn_parent = tt; /* parent */
		}
#ifdef RN_DEBUG
		t=tt+1; tt->rn_info = rn_nodenum++; t->rn_info = rn_nodenum++;
		tt->rn_twin = t; tt->rn_ybro = rn_clist; rn_clist = tt;
#endif
		tt->rn_key = key;
		tt->rn_bit = -1;
		tt->rn_flags = RNF_ACTIVE;
	}
	/*
	 * Put mask in tree.
	 */
	if (netmask != NULL) {
		tt->rn_mask = netmask;
		tt->rn_bit = x->rn_bit;
		tt->rn_flags |= x->rn_flags & RNF_NORMAL;
	}
	t = saved_tt->rn_parent;
	if (keyduplicated)
		goto on2;
	b_leaf = -1 - t->rn_bit;
	if (t->rn_right == saved_tt)
		x = t->rn_left;
	else
		x = t->rn_right;
	/* Promote general routes from below */
	if (x->rn_bit < 0) {
		mp = &t->rn_mklist;
		while (x != NULL) {
			if (x->rn_mask != NULL &&
			    x->rn_bit >= b_leaf &&
			    x->rn_mklist == NULL) {
				*mp = m = rn_new_radix_mask(x, NULL);
				if (m != NULL)
					mp = &m->rm_next;
			}
			x = x->rn_dupedkey;
		}
	} else if (x->rn_mklist != NULL) {
		/*
		 * Skip over masks whose index is > that of new node
		 */
		for (mp = &x->rn_mklist; (m = *mp); mp = &m->rm_next)
			if (m->rm_bit >= b_leaf)
				break;
		t->rn_mklist = m;
		*mp = NULL;
	}
on2:
	/* Add new route to highest possible ancestor's list */
	if ((netmask == NULL) || (b > t->rn_bit ))
		return tt; /* can't lift at all */
	b_leaf = tt->rn_bit;
	do {
		x = t;
		t = t->rn_parent;
	} while (b <= t->rn_bit && x != top);
	/*
	 * Search through routes associated with node to
	 * insert new route according to index.
	 * Need same criteria as when sorting dupedkeys to avoid
	 * double loop on deletion.
	 */
	for (mp = &x->rn_mklist; (m = *mp); mp = &m->rm_next) {
		if (m->rm_bit < b_leaf)
			continue;
		if (m->rm_bit > b_leaf)
			break;
		if (m->rm_flags & RNF_NORMAL) {
			mmask = m->rm_leaf->rn_mask;
			if (tt->rn_flags & RNF_NORMAL) {
			    log(LOG_ERR,
			        "Non-unique normal route, mask not entered\n");
				return tt;
			}
		} else
			mmask = m->rm_mask;
		if (mmask == netmask) {
			m->rm_refs++;
			tt->rn_mklist = m;
			return tt;
		}
		if (rn_refines(netmask, mmask) || rn_lexobetter(netmask, mmask))
			break;
	}
	*mp = rn_new_radix_mask(tt, *mp);
	return tt;
}

struct radix_node *
rn_delete(char *key, char *netmask, struct radix_node_head *head)
{
	struct radix_node *t, *p, *x, *tt;
	struct radix_mask *m, *saved_m, **mp;
	struct radix_node *dupedkey, *saved_tt, *top;
	int b, head_off, klen;
	int cpu = mycpuid;

	x = head->rnh_treetop;
	tt = rn_search(key, x);
	head_off = x->rn_offset;
	klen =  clen(key);
	saved_tt = tt;
	top = x;
	if (tt == NULL ||
	    bcmp(key + head_off, tt->rn_key + head_off, klen - head_off))
		return (NULL);
	/*
	 * Delete our route from mask lists.
	 */
	if (netmask != NULL) {
		if ((x = rn_addmask(netmask, TRUE, head_off,
				    head->rnh_maskhead)) == NULL)
			return (NULL);
		netmask = x->rn_key;
		while (tt->rn_mask != netmask)
			if ((tt = tt->rn_dupedkey) == NULL)
				return (NULL);
	}
	if (tt->rn_mask == NULL || (saved_m = m = tt->rn_mklist) == NULL)
		goto on1;
	if (tt->rn_flags & RNF_NORMAL) {
		if (m->rm_leaf != tt || m->rm_refs > 0) {
			log(LOG_ERR, "rn_delete: inconsistent annotation\n");
			return (NULL);  /* dangling ref could cause disaster */
		}
	} else {
		if (m->rm_mask != tt->rn_mask) {
			log(LOG_ERR, "rn_delete: inconsistent annotation\n");
			goto on1;
		}
		if (--m->rm_refs >= 0)
			goto on1;
	}
	b = -1 - tt->rn_bit;
	t = saved_tt->rn_parent;
	if (b > t->rn_bit)
		goto on1; /* Wasn't lifted at all */
	do {
		x = t;
		t = t->rn_parent;
	} while (b <= t->rn_bit && x != top);
	for (mp = &x->rn_mklist; (m = *mp); mp = &m->rm_next)
		if (m == saved_m) {
			*mp = m->rm_next;
			MKFree(&rn_mkfreelist[cpu], m);
			break;
		}
	if (m == NULL) {
		log(LOG_ERR, "rn_delete: couldn't find our annotation\n");
		if (tt->rn_flags & RNF_NORMAL)
			return (NULL); /* Dangling ref to us */
	}
on1:
	/*
	 * Eliminate us from tree
	 */
	if (tt->rn_flags & RNF_ROOT)
		return (NULL);
#ifdef RN_DEBUG
	/* Get us out of the creation list */
	for (t = rn_clist; t && t->rn_ybro != tt; t = t->rn_ybro) {}
	if (t) t->rn_ybro = tt->rn_ybro;
#endif
	t = tt->rn_parent;
	dupedkey = saved_tt->rn_dupedkey;
	if (dupedkey != NULL) {
		/*
		 * at this point, tt is the deletion target and saved_tt
		 * is the head of the dupekey chain
		 */
		if (tt == saved_tt) {
			/* remove from head of chain */
			x = dupedkey; x->rn_parent = t;
			if (t->rn_left == tt)
				t->rn_left = x;
			else
				t->rn_right = x;
		} else {
			/* find node in front of tt on the chain */
			for (x = p = saved_tt; p && p->rn_dupedkey != tt;)
				p = p->rn_dupedkey;
			if (p) {
				p->rn_dupedkey = tt->rn_dupedkey;
				if (tt->rn_dupedkey)		/* parent */
					tt->rn_dupedkey->rn_parent = p;
								/* parent */
			} else log(LOG_ERR, "rn_delete: couldn't find us\n");
		}
		t = tt + 1;
		if  (t->rn_flags & RNF_ACTIVE) {
#ifndef RN_DEBUG
			*++x = *t;
			p = t->rn_parent;
#else
			b = t->rn_info;
			*++x = *t;
			t->rn_info = b;
			p = t->rn_parent;
#endif
			if (p->rn_left == t)
				p->rn_left = x;
			else
				p->rn_right = x;
			x->rn_left->rn_parent = x;
			x->rn_right->rn_parent = x;
		}
		goto out;
	}
	if (t->rn_left == tt)
		x = t->rn_right;
	else
		x = t->rn_left;
	p = t->rn_parent;
	if (p->rn_right == t)
		p->rn_right = x;
	else
		p->rn_left = x;
	x->rn_parent = p;
	/*
	 * Demote routes attached to us.
	 */
	if (t->rn_mklist != NULL) {
		if (x->rn_bit >= 0) {
			for (mp = &x->rn_mklist; (m = *mp);)
				mp = &m->rm_next;
			*mp = t->rn_mklist;
		} else {
			/*
			 * If there are any (key, mask) pairs in a sibling
			 * duped-key chain, some subset will appear sorted
			 * in the same order attached to our mklist.
			 */
			for (m = t->rn_mklist; m && x; x = x->rn_dupedkey)
				if (m == x->rn_mklist) {
					struct radix_mask *mm = m->rm_next;

					x->rn_mklist = NULL;
					if (--(m->rm_refs) < 0)
						MKFree(&rn_mkfreelist[cpu], m);
					m = mm;
				}
			if (m)
				log(LOG_ERR,
				    "rn_delete: Orphaned Mask %p at %p\n",
				    (void *)m, (void *)x);
		}
	}
	/*
	 * We may be holding an active internal node in the tree.
	 */
	x = tt + 1;
	if (t != x) {
#ifndef RN_DEBUG
		*t = *x;
#else
		b = t->rn_info;
		*t = *x;
		t->rn_info = b;
#endif
		t->rn_left->rn_parent = t;
		t->rn_right->rn_parent = t;
		p = x->rn_parent;
		if (p->rn_left == x)
			p->rn_left = t;
		else
			p->rn_right = t;
	}
out:
	tt->rn_flags &= ~RNF_ACTIVE;
	tt[1].rn_flags &= ~RNF_ACTIVE;
	return (tt);
}

/*
 * This is the same as rn_walktree() except for the parameters and the
 * exit.
 */
static int
rn_walktree_from(struct radix_node_head *h, char *xa, char *xm,
		 walktree_f_t *f, void *w)
{
	struct radix_node *base, *next;
	struct radix_node *rn, *last = NULL /* shut up gcc */;
	boolean_t stopping = FALSE;
	int lastb, error;

	/*
	 * rn_search_m is sort-of-open-coded here.
	 */
	/* kprintf("about to search\n"); */
	for (rn = h->rnh_treetop; rn->rn_bit >= 0; ) {
		last = rn;
		/* kprintf("rn_bit %d, rn_bmask %x, xm[rn_offset] %x\n",
		       rn->rn_bit, rn->rn_bmask, xm[rn->rn_offset]); */
		if (!(rn->rn_bmask & xm[rn->rn_offset])) {
			break;
		}
		if (rn->rn_bmask & xa[rn->rn_offset]) {
			rn = rn->rn_right;
		} else {
			rn = rn->rn_left;
		}
	}
	/* kprintf("done searching\n"); */

	/*
	 * Two cases: either we stepped off the end of our mask,
	 * in which case last == rn, or we reached a leaf, in which
	 * case we want to start from the last node we looked at.
	 * Either way, last is the node we want to start from.
	 */
	rn = last;
	lastb = rn->rn_bit;

	/* kprintf("rn %p, lastb %d\n", rn, lastb);*/

	/*
	 * This gets complicated because we may delete the node
	 * while applying the function f to it, so we need to calculate
	 * the successor node in advance.
	 */
	while (rn->rn_bit >= 0)
		rn = rn->rn_left;

	while (!stopping) {
		/* kprintf("node %p (%d)\n", rn, rn->rn_bit); */
		base = rn;
		/* If at right child go back up, otherwise, go right */
		while (rn->rn_parent->rn_right == rn &&
		    !(rn->rn_flags & RNF_ROOT)) {
			rn = rn->rn_parent;

			/* if went up beyond last, stop */
			if (rn->rn_bit < lastb) {
				stopping = TRUE;
				/* kprintf("up too far\n"); */
			}
		}

		/* Find the next *leaf* since next node might vanish, too */
		for (rn = rn->rn_parent->rn_right; rn->rn_bit >= 0;)
			rn = rn->rn_left;
		next = rn;
		/* Process leaves */
		while ((rn = base) != NULL) {
			base = rn->rn_dupedkey;
			/* kprintf("leaf %p\n", rn); */
			if (!(rn->rn_flags & RNF_ROOT) && (error = (*f)(rn, w)))
				return (error);
		}
		rn = next;

		if (rn->rn_flags & RNF_ROOT) {
			/* kprintf("root, stopping"); */
			stopping = TRUE;
		}

	}
	return 0;
}

static int
rn_walktree(struct radix_node_head *h, walktree_f_t *f, void *w)
{
	struct radix_node *base, *next;
	struct radix_node *rn = h->rnh_treetop;
	int error;

	/*
	 * This gets complicated because we may delete the node
	 * while applying the function f to it, so we need to calculate
	 * the successor node in advance.
	 */
	/* First time through node, go left */
	while (rn->rn_bit >= 0)
		rn = rn->rn_left;
	for (;;) {
		base = rn;
		/* If at right child go back up, otherwise, go right */
		while (rn->rn_parent->rn_right == rn &&
		    !(rn->rn_flags & RNF_ROOT))
			rn = rn->rn_parent;
		/* Find the next *leaf* since next node might vanish, too */
		for (rn = rn->rn_parent->rn_right; rn->rn_bit >= 0;)
			rn = rn->rn_left;
		next = rn;
		/* Process leaves */
		while ((rn = base)) {
			base = rn->rn_dupedkey;
			if (!(rn->rn_flags & RNF_ROOT) && (error = (*f)(rn, w)))
				return (error);
		}
		rn = next;
		if (rn->rn_flags & RNF_ROOT)
			return (0);
	}
	/* NOTREACHED */
}

int
rn_inithead(void **head, struct radix_node_head *maskhead, int off)
{
	struct radix_node_head *rnh;
	struct radix_node *root, *left, *right;

	if (*head != NULL)	/* already initialized */
		return (1);

	R_Malloc(rnh, struct radix_node_head *, sizeof *rnh);
	if (rnh == NULL)
		return (0);
	bzero(rnh, sizeof *rnh);
	*head = rnh;

	root = rn_newpair(rn_zeros, off, rnh->rnh_nodes);
	right = &rnh->rnh_nodes[2];
	root->rn_parent = root;
	root->rn_flags = RNF_ROOT | RNF_ACTIVE;
	root->rn_right = right;

	left = root->rn_left;
	left->rn_bit = -1 - off;
	left->rn_flags = RNF_ROOT | RNF_ACTIVE;

	*right = *left;
	right->rn_key = rn_ones;

	rnh->rnh_treetop = root;
	rnh->rnh_maskhead = maskhead;

	rnh->rnh_addaddr = rn_addroute;
	rnh->rnh_deladdr = rn_delete;
	rnh->rnh_matchaddr = rn_match;
	rnh->rnh_lookup = rn_lookup;
	rnh->rnh_walktree = rn_walktree;
	rnh->rnh_walktree_from = rn_walktree_from;

	return (1);
}

void
rn_init(void)
{
	int cpu;
#ifdef _KERNEL
	struct domain *dom;

	SLIST_FOREACH(dom, &domains, dom_next) {
		if (dom->dom_maxrtkey > RN_MAXKEYLEN) {
			panic("domain %s maxkey too big %d/%d",
			      dom->dom_name, dom->dom_maxrtkey, RN_MAXKEYLEN);
		}
	}
#endif
	for (cpu = 0; cpu < ncpus; ++cpu) {
		if (rn_inithead((void **)&mask_rnheads[cpu], NULL, 0) == 0)
			panic("rn_init 2");
	}
}

struct radix_node_head *
rn_cpumaskhead(int cpu)
{
	KKASSERT(mask_rnheads[cpu] != NULL);
	return mask_rnheads[cpu];
}
