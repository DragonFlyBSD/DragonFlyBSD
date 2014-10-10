/* kwset.c - search for any of a set of keywords.
   Copyright (C) 1989, 1998, 2000, 2005, 2007, 2009-2014 Free Software
   Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Written August 1989 by Mike Haertel.
   The author may be reached (Email) at the address mike@ai.mit.edu,
   or (US mail) as Mike Haertel c/o Free Software Foundation. */

/* The algorithm implemented by these routines bears a startling resemblance
   to one discovered by Beate Commentz-Walter, although it is not identical.
   See: Commentz-Walter B. A string matching algorithm fast on the average.
   Lecture Notes in Computer Science 71 (1979), 118-32
   <http://dx.doi.org/10.1007/3-540-09510-1_10>.
   See also: Aho AV, Corasick MJ. Efficient string matching: an aid to
   bibliographic search. CACM 18, 6 (1975), 333-40
   <http://dx.doi.org/10.1145/360825.360855>, which describes the
   failure function used below. */

#include <config.h>

#include "kwset.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "system.h"
#include "memchr2.h"
#include "obstack.h"
#include "xalloc.h"

#define link kwset_link

#ifdef GREP
# include "xalloc.h"
# undef malloc
# define malloc xmalloc
#endif

#define NCHAR (UCHAR_MAX + 1)
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

#define U(c) (to_uchar (c))

/* Balanced tree of edges and labels leaving a given trie node. */
struct tree
{
  struct tree *llink;		/* Left link; MUST be first field. */
  struct tree *rlink;		/* Right link (to larger labels). */
  struct trie *trie;		/* Trie node pointed to by this edge. */
  unsigned char label;		/* Label on this edge. */
  char balance;			/* Difference in depths of subtrees. */
};

/* Node of a trie representing a set of reversed keywords. */
struct trie
{
  size_t accepting;		/* Word index of accepted word, or zero. */
  struct tree *links;		/* Tree of edges leaving this node. */
  struct trie *parent;		/* Parent of this node. */
  struct trie *next;		/* List of all trie nodes in level order. */
  struct trie *fail;		/* Aho-Corasick failure function. */
  int depth;			/* Depth of this node from the root. */
  int shift;			/* Shift function for search failures. */
  int maxshift;			/* Max shift of self and descendants. */
};

/* Structure returned opaquely to the caller, containing everything. */
struct kwset
{
  struct obstack obstack;	/* Obstack for node allocation. */
  ptrdiff_t words;		/* Number of words in the trie. */
  struct trie *trie;		/* The trie itself. */
  int mind;			/* Minimum depth of an accepting node. */
  int maxd;			/* Maximum depth of any node. */
  unsigned char delta[NCHAR];	/* Delta table for rapid search. */
  struct trie *next[NCHAR];	/* Table of children of the root. */
  char *target;			/* Target string if there's only one. */
  int *shift;			/* Used in Boyer-Moore search for one string. */
  char const *trans;		/* Character translation table. */

  /* If there's only one string, this is the string's last byte,
     translated via TRANS if TRANS is nonnull.  */
  char gc1;

  /* Likewise for the string's penultimate byte, if it has two or more
     bytes.  */
  char gc2;

  /* If there's only one string, this helps to match the string's last byte.
     If GC1HELP is negative, only GC1 matches the string's last byte;
     otherwise at least two bytes match, and B matches if TRANS[B] == GC1.
     If GC1HELP is in the range 0..(NCHAR - 1), there are exactly two
     such matches, and GC1HELP is the other match after conversion to
     unsigned char.  If GC1HELP is at least NCHAR, there are three or
     more such matches; e.g., Greek has three sigma characters that
     all match when case-folding.  */
  int gc1help;
};

/* Use TRANS to transliterate C.  A null TRANS does no transliteration.  */
static inline char
tr (char const *trans, char c)
{
  return trans ? trans[U(c)] : c;
}

/* Allocate and initialize a keyword set object, returning an opaque
   pointer to it.  */
kwset_t
kwsalloc (char const *trans)
{
  struct kwset *kwset = xmalloc (sizeof *kwset);

  obstack_init (&kwset->obstack);
  kwset->words = 0;
  kwset->trie = obstack_alloc (&kwset->obstack, sizeof *kwset->trie);
  kwset->trie->accepting = 0;
  kwset->trie->links = NULL;
  kwset->trie->parent = NULL;
  kwset->trie->next = NULL;
  kwset->trie->fail = NULL;
  kwset->trie->depth = 0;
  kwset->trie->shift = 0;
  kwset->mind = INT_MAX;
  kwset->maxd = -1;
  kwset->target = NULL;
  kwset->trans = trans;

  return kwset;
}

/* This upper bound is valid for CHAR_BIT >= 4 and
   exact for CHAR_BIT in { 4..11, 13, 15, 17, 19 }. */
#define DEPTH_SIZE (CHAR_BIT + CHAR_BIT/2)

/* Add the given string to the contents of the keyword set.  */
void
kwsincr (kwset_t kwset, char const *text, size_t len)
{
  struct trie *trie = kwset->trie;
  char const *trans = kwset->trans;

  text += len;

  /* Descend the trie (built of reversed keywords) character-by-character,
     installing new nodes when necessary. */
  while (len--)
    {
      unsigned char uc = *--text;
      unsigned char label = trans ? trans[uc] : uc;

      /* Descend the tree of outgoing links for this trie node,
         looking for the current character and keeping track
         of the path followed. */
      struct tree *link = trie->links;
      struct tree *links[DEPTH_SIZE];
      enum { L, R } dirs[DEPTH_SIZE];
      links[0] = (struct tree *) &trie->links;
      dirs[0] = L;
      int depth = 1;

      while (link && label != link->label)
        {
          links[depth] = link;
          if (label < link->label)
            dirs[depth++] = L, link = link->llink;
          else
            dirs[depth++] = R, link = link->rlink;
        }

      /* The current character doesn't have an outgoing link at
         this trie node, so build a new trie node and install
         a link in the current trie node's tree. */
      if (!link)
        {
          link = obstack_alloc (&kwset->obstack, sizeof *link);
          link->llink = NULL;
          link->rlink = NULL;
          link->trie = obstack_alloc (&kwset->obstack, sizeof *link->trie);
          link->trie->accepting = 0;
          link->trie->links = NULL;
          link->trie->parent = trie;
          link->trie->next = NULL;
          link->trie->fail = NULL;
          link->trie->depth = trie->depth + 1;
          link->trie->shift = 0;
          link->label = label;
          link->balance = 0;

          /* Install the new tree node in its parent. */
          if (dirs[--depth] == L)
            links[depth]->llink = link;
          else
            links[depth]->rlink = link;

          /* Back up the tree fixing the balance flags. */
          while (depth && !links[depth]->balance)
            {
              if (dirs[depth] == L)
                --links[depth]->balance;
              else
                ++links[depth]->balance;
              --depth;
            }

          /* Rebalance the tree by pointer rotations if necessary. */
          if (depth && ((dirs[depth] == L && --links[depth]->balance)
                        || (dirs[depth] == R && ++links[depth]->balance)))
            {
              struct tree *t, *r, *l, *rl, *lr;

              switch (links[depth]->balance)
                {
                case (char) -2:
                  switch (dirs[depth + 1])
                    {
                    case L:
                      r = links[depth], t = r->llink, rl = t->rlink;
                      t->rlink = r, r->llink = rl;
                      t->balance = r->balance = 0;
                      break;
                    case R:
                      r = links[depth], l = r->llink, t = l->rlink;
                      rl = t->rlink, lr = t->llink;
                      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
                      l->balance = t->balance != 1 ? 0 : -1;
                      r->balance = t->balance != (char) -1 ? 0 : 1;
                      t->balance = 0;
                      break;
                    default:
                      abort ();
                    }
                  break;
                case 2:
                  switch (dirs[depth + 1])
                    {
                    case R:
                      l = links[depth], t = l->rlink, lr = t->llink;
                      t->llink = l, l->rlink = lr;
                      t->balance = l->balance = 0;
                      break;
                    case L:
                      l = links[depth], r = l->rlink, t = r->llink;
                      lr = t->llink, rl = t->rlink;
                      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
                      l->balance = t->balance != 1 ? 0 : -1;
                      r->balance = t->balance != (char) -1 ? 0 : 1;
                      t->balance = 0;
                      break;
                    default:
                      abort ();
                    }
                  break;
                default:
                  abort ();
                }

              if (dirs[depth - 1] == L)
                links[depth - 1]->llink = t;
              else
                links[depth - 1]->rlink = t;
            }
        }

      trie = link->trie;
    }

  /* Mark the node we finally reached as accepting, encoding the
     index number of this word in the keyword set so far. */
  if (!trie->accepting)
    trie->accepting = 1 + 2 * kwset->words;
  ++kwset->words;

  /* Keep track of the longest and shortest string of the keyword set. */
  if (trie->depth < kwset->mind)
    kwset->mind = trie->depth;
  if (trie->depth > kwset->maxd)
    kwset->maxd = trie->depth;
}

/* Enqueue the trie nodes referenced from the given tree in the
   given queue. */
static void
enqueue (struct tree *tree, struct trie **last)
{
  if (!tree)
    return;
  enqueue(tree->llink, last);
  enqueue(tree->rlink, last);
  (*last) = (*last)->next = tree->trie;
}

/* Compute the Aho-Corasick failure function for the trie nodes referenced
   from the given tree, given the failure function for their parent as
   well as a last resort failure node. */
static void
treefails (struct tree const *tree, struct trie const *fail,
           struct trie *recourse)
{
  struct tree *link;

  if (!tree)
    return;

  treefails(tree->llink, fail, recourse);
  treefails(tree->rlink, fail, recourse);

  /* Find, in the chain of fails going back to the root, the first
     node that has a descendant on the current label. */
  while (fail)
    {
      link = fail->links;
      while (link && tree->label != link->label)
        if (tree->label < link->label)
          link = link->llink;
        else
          link = link->rlink;
      if (link)
        {
          tree->trie->fail = link->trie;
          return;
        }
      fail = fail->fail;
    }

  tree->trie->fail = recourse;
}

/* Set delta entries for the links of the given tree such that
   the preexisting delta value is larger than the current depth. */
static void
treedelta (struct tree const *tree,
           unsigned int depth,
           unsigned char delta[])
{
  if (!tree)
    return;
  treedelta(tree->llink, depth, delta);
  treedelta(tree->rlink, depth, delta);
  if (depth < delta[tree->label])
    delta[tree->label] = depth;
}

/* Return true if A has every label in B. */
static int _GL_ATTRIBUTE_PURE
hasevery (struct tree const *a, struct tree const *b)
{
  if (!b)
    return 1;
  if (!hasevery(a, b->llink))
    return 0;
  if (!hasevery(a, b->rlink))
    return 0;
  while (a && b->label != a->label)
    if (b->label < a->label)
      a = a->llink;
    else
      a = a->rlink;
  return !!a;
}

/* Compute a vector, indexed by character code, of the trie nodes
   referenced from the given tree. */
static void
treenext (struct tree const *tree, struct trie *next[])
{
  if (!tree)
    return;
  treenext(tree->llink, next);
  treenext(tree->rlink, next);
  next[tree->label] = tree->trie;
}

/* Compute the shift for each trie node, as well as the delta
   table and next cache for the given keyword set. */
void
kwsprep (kwset_t kwset)
{
  char const *trans = kwset->trans;
  int i;
  unsigned char deltabuf[NCHAR];
  unsigned char *delta = trans ? deltabuf : kwset->delta;

  /* Initial values for the delta table; will be changed later.  The
     delta entry for a given character is the smallest depth of any
     node at which an outgoing edge is labeled by that character. */
  memset (delta, MIN (kwset->mind, UCHAR_MAX), sizeof deltabuf);

  /* Traverse the nodes of the trie in level order, simultaneously
     computing the delta table, failure function, and shift function.  */
  struct trie *curr, *last;
  for (curr = last = kwset->trie; curr; curr = curr->next)
    {
      /* Enqueue the immediate descendants in the level order queue.  */
      enqueue (curr->links, &last);

      curr->shift = kwset->mind;
      curr->maxshift = kwset->mind;

      /* Update the delta table for the descendants of this node.  */
      treedelta (curr->links, curr->depth, delta);

      /* Compute the failure function for the descendants of this node.  */
      treefails (curr->links, curr->fail, kwset->trie);

      /* Update the shifts at each node in the current node's chain
         of fails back to the root.  */
      struct trie *fail;
      for (fail = curr->fail; fail; fail = fail->fail)
        {
          /* If the current node has some outgoing edge that the fail
             doesn't, then the shift at the fail should be no larger
             than the difference of their depths.  */
          if (!hasevery (fail->links, curr->links))
            if (curr->depth - fail->depth < fail->shift)
              fail->shift = curr->depth - fail->depth;

          /* If the current node is accepting then the shift at the
             fail and its descendants should be no larger than the
             difference of their depths.  */
          if (curr->accepting && fail->maxshift > curr->depth - fail->depth)
            fail->maxshift = curr->depth - fail->depth;
        }
    }

  /* Traverse the trie in level order again, fixing up all nodes whose
     shift exceeds their inherited maxshift.  */
  for (curr = kwset->trie->next; curr; curr = curr->next)
    {
      if (curr->maxshift > curr->parent->maxshift)
        curr->maxshift = curr->parent->maxshift;
      if (curr->shift > curr->maxshift)
        curr->shift = curr->maxshift;
    }

  /* Create a vector, indexed by character code, of the outgoing links
     from the root node.  */
  struct trie *nextbuf[NCHAR];
  struct trie **next = trans ? nextbuf : kwset->next;
  memset (next, 0, sizeof nextbuf);
  treenext (kwset->trie->links, next);
  if (trans)
    for (i = 0; i < NCHAR; ++i)
      kwset->next[i] = next[U(trans[i])];

  /* Check if we can use the simple boyer-moore algorithm, instead
     of the hairy commentz-walter algorithm. */
  if (kwset->words == 1)
    {
      /* Looking for just one string.  Extract it from the trie. */
      kwset->target = obstack_alloc (&kwset->obstack, kwset->mind);
      for (i = kwset->mind - 1, curr = kwset->trie; i >= 0; --i)
        {
          kwset->target[i] = curr->links->label;
          curr = curr->next;
        }
      /* Looking for the delta2 shift that we might make after a
         backwards match has failed.  Extract it from the trie.  */
      if (kwset->mind > 1)
        {
          kwset->shift
            = obstack_alloc (&kwset->obstack,
                             sizeof *kwset->shift * (kwset->mind - 1));
          for (i = 0, curr = kwset->trie->next; i < kwset->mind - 1; ++i)
            {
              kwset->shift[i] = curr->shift;
              curr = curr->next;
            }
        }

      char gc1 = tr (trans, kwset->target[kwset->mind - 1]);

      /* Set GC1HELP according to whether exactly one, exactly two, or
         three-or-more characters match GC1.  */
      int gc1help = -1;
      if (trans)
        {
          char const *equiv1 = memchr (trans, gc1, NCHAR);
          char const *equiv2 = memchr (equiv1 + 1, gc1,
                                       trans + NCHAR - (equiv1 + 1));
          if (equiv2)
            gc1help = (memchr (equiv2 + 1, gc1, trans + NCHAR - (equiv2 + 1))
                       ? NCHAR
                       : U(gc1) ^ (equiv1 - trans) ^ (equiv2 - trans));
        }

      kwset->gc1 = gc1;
      kwset->gc1help = gc1help;
      if (kwset->mind > 1)
        kwset->gc2 = tr (trans, kwset->target[kwset->mind - 2]);
    }

  /* Fix things up for any translation table. */
  if (trans)
    for (i = 0; i < NCHAR; ++i)
      kwset->delta[i] = delta[U(trans[i])];
}

/* Delta2 portion of a Boyer-Moore search.  *TP is the string text
   pointer; it is updated in place.  EP is the end of the string text,
   and SP the end of the pattern.  LEN is the pattern length; it must
   be at least 2.  TRANS, if nonnull, is the input translation table.
   GC1 and GC2 are the last and second-from last bytes of the pattern,
   transliterated by TRANS; the caller precomputes them for
   efficiency.  If D1 is nonnull, it is a delta1 table for shifting *TP
   when failing.  KWSET->shift says how much to shift.  */
static inline bool
bm_delta2_search (char const **tpp, char const *ep, char const *sp, int len,
                  char const *trans, char gc1, char gc2,
                  unsigned char const *d1, kwset_t kwset)
{
  char const *tp = *tpp;
  int d = len, skip = 0;

  while (true)
    {
      int i = 2;
      if (tr (trans, tp[-2]) == gc2)
        {
          while (++i <= d)
            if (tr (trans, tp[-i]) != tr (trans, sp[-i]))
              break;
          if (i > d)
            {
              for (i = d + skip + 1; i <= len; ++i)
                if (tr (trans, tp[-i]) != tr (trans, sp[-i]))
                  break;
              if (i > len)
                {
                  *tpp = tp - len;
                  return true;
                }
            }
        }

      tp += d = kwset->shift[i - 2];
      if (tp > ep)
        break;
      if (tr (trans, tp[-1]) != gc1)
        {
          if (d1)
            tp += d1[U(tp[-1])];
          break;
        }
      skip = i - 1;
    }

  *tpp = tp;
  return false;
}

/* Return the address of the first byte in the buffer S (of size N)
   that matches the last byte specified by KWSET, a singleton.  */
static char const *
memchr_kwset (char const *s, size_t n, kwset_t kwset)
{
  if (kwset->gc1help < 0)
    return memchr (s, kwset->gc1, n);
  int small_heuristic = 2;
  int small = (- (uintptr_t) s % sizeof (long)
               + small_heuristic * sizeof (long));
  size_t ntrans = kwset->gc1help < NCHAR && small < n ? small : n;
  char const *slim = s + ntrans;
  for (; s < slim; s++)
    if (kwset->trans[U(*s)] == kwset->gc1)
      return s;
  n -= ntrans;
  return n == 0 ? NULL : memchr2 (s, kwset->gc1, kwset->gc1help, n);
}

/* Fast Boyer-Moore search (inlinable version).  */
static inline size_t _GL_ATTRIBUTE_PURE
bmexec_trans (kwset_t kwset, char const *text, size_t size)
{
  unsigned char const *d1;
  char const *ep, *sp, *tp;
  int d;
  int len = kwset->mind;
  char const *trans = kwset->trans;

  if (len == 0)
    return 0;
  if (len > size)
    return -1;
  if (len == 1)
    {
      tp = memchr_kwset (text, size, kwset);
      return tp ? tp - text : -1;
    }

  d1 = kwset->delta;
  sp = kwset->target + len;
  tp = text + len;
  char gc1 = kwset->gc1;
  char gc2 = kwset->gc2;

  /* Significance of 12: 1 (initial offset) + 10 (skip loop) + 1 (md2). */
  if (size > 12 * len)
    /* 11 is not a bug, the initial offset happens only once. */
    for (ep = text + size - 11 * len; tp <= ep; )
      {
        char const *tp0 = tp;
        d = d1[U(tp[-1])], tp += d;
        d = d1[U(tp[-1])], tp += d;
        if (d != 0)
          {
            d = d1[U(tp[-1])], tp += d;
            d = d1[U(tp[-1])], tp += d;
            d = d1[U(tp[-1])], tp += d;
            if (d != 0)
              {
                d = d1[U(tp[-1])], tp += d;
                d = d1[U(tp[-1])], tp += d;
                d = d1[U(tp[-1])], tp += d;
                if (d != 0)
                  {
                    d = d1[U(tp[-1])], tp += d;
                    d = d1[U(tp[-1])], tp += d;

                    /* As a heuristic, prefer memchr to seeking by
                       delta1 when the latter doesn't advance much.  */
                    int advance_heuristic = 16 * sizeof (long);
                    if (advance_heuristic <= tp - tp0)
                      goto big_advance;
                    tp--;
                    tp = memchr_kwset (tp, text + size - tp, kwset);
                    if (! tp)
                      return -1;
                    tp++;
                  }
              }
          }
        if (bm_delta2_search (&tp, ep, sp, len, trans, gc1, gc2, d1, kwset))
          return tp - text;
      big_advance:;
      }

  /* Now we have only a few characters left to search.  We
     carefully avoid ever producing an out-of-bounds pointer. */
  ep = text + size;
  d = d1[U(tp[-1])];
  while (d <= ep - tp)
    {
      d = d1[U((tp += d)[-1])];
      if (d != 0)
        continue;
      if (bm_delta2_search (&tp, ep, sp, len, trans, gc1, gc2, NULL, kwset))
        return tp - text;
    }

  return -1;
}

/* Fast Boyer-Moore search.  */
static size_t
bmexec (kwset_t kwset, char const *text, size_t size)
{
  /* Help the compiler inline bmexec_trans in two ways, depending on
     whether kwset->trans is null.  */
  return (kwset->trans
          ? bmexec_trans (kwset, text, size)
          : bmexec_trans (kwset, text, size));
}

/* Hairy multiple string search. */
static size_t _GL_ARG_NONNULL ((4))
cwexec (kwset_t kwset, char const *text, size_t len, struct kwsmatch *kwsmatch)
{
  struct trie * const *next;
  struct trie const *trie;
  struct trie const *accept;
  char const *beg, *lim, *mch, *lmch;
  unsigned char c;
  unsigned char const *delta;
  int d;
  char const *end, *qlim;
  struct tree const *tree;
  char const *trans;

#ifdef lint
  accept = NULL;
#endif

  /* Initialize register copies and look for easy ways out. */
  if (len < kwset->mind)
    return -1;
  next = kwset->next;
  delta = kwset->delta;
  trans = kwset->trans;
  lim = text + len;
  end = text;
  if ((d = kwset->mind) != 0)
    mch = NULL;
  else
    {
      mch = text, accept = kwset->trie;
      goto match;
    }

  if (len >= 4 * kwset->mind)
    qlim = lim - 4 * kwset->mind;
  else
    qlim = NULL;

  while (lim - end >= d)
    {
      if (qlim && end <= qlim)
        {
          end += d - 1;
          while ((d = delta[c = *end]) && end < qlim)
            {
              end += d;
              end += delta[U(*end)];
              end += delta[U(*end)];
            }
          ++end;
        }
      else
        d = delta[c = (end += d)[-1]];
      if (d)
        continue;
      beg = end - 1;
      trie = next[c];
      if (trie->accepting)
        {
          mch = beg;
          accept = trie;
        }
      d = trie->shift;
      while (beg > text)
        {
          unsigned char uc = *--beg;
          c = trans ? trans[uc] : uc;
          tree = trie->links;
          while (tree && c != tree->label)
            if (c < tree->label)
              tree = tree->llink;
            else
              tree = tree->rlink;
          if (tree)
            {
              trie = tree->trie;
              if (trie->accepting)
                {
                  mch = beg;
                  accept = trie;
                }
            }
          else
            break;
          d = trie->shift;
        }
      if (mch)
        goto match;
    }
  return -1;

 match:
  /* Given a known match, find the longest possible match anchored
     at or before its starting point.  This is nearly a verbatim
     copy of the preceding main search loops. */
  if (lim - mch > kwset->maxd)
    lim = mch + kwset->maxd;
  lmch = 0;
  d = 1;
  while (lim - end >= d)
    {
      if ((d = delta[c = (end += d)[-1]]) != 0)
        continue;
      beg = end - 1;
      if (!(trie = next[c]))
        {
          d = 1;
          continue;
        }
      if (trie->accepting && beg <= mch)
        {
          lmch = beg;
          accept = trie;
        }
      d = trie->shift;
      while (beg > text)
        {
          unsigned char uc = *--beg;
          c = trans ? trans[uc] : uc;
          tree = trie->links;
          while (tree && c != tree->label)
            if (c < tree->label)
              tree = tree->llink;
            else
              tree = tree->rlink;
          if (tree)
            {
              trie = tree->trie;
              if (trie->accepting && beg <= mch)
                {
                  lmch = beg;
                  accept = trie;
                }
            }
          else
            break;
          d = trie->shift;
        }
      if (lmch)
        {
          mch = lmch;
          goto match;
        }
      if (!d)
        d = 1;
    }

  kwsmatch->index = accept->accepting / 2;
  kwsmatch->offset[0] = mch - text;
  kwsmatch->size[0] = accept->depth;

  return mch - text;
}

/* Search TEXT for a match of any member of KWSET.
   Return the offset (into TEXT) of the first byte of the matching substring,
   or (size_t) -1 if no match is found.  Upon a match, store details in
   *KWSMATCH: index of matched keyword, start offset (same as the return
   value), and length.  */
size_t
kwsexec (kwset_t kwset, char const *text, size_t size,
         struct kwsmatch *kwsmatch)
{
  if (kwset->words == 1)
    {
      size_t ret = bmexec (kwset, text, size);
      if (ret != (size_t) -1)
        {
          kwsmatch->index = 0;
          kwsmatch->offset[0] = ret;
          kwsmatch->size[0] = kwset->mind;
        }
      return ret;
    }
  else
    return cwexec (kwset, text, size, kwsmatch);
}

/* Free the components of the given keyword set. */
void
kwsfree (kwset_t kwset)
{
  obstack_free (&kwset->obstack, NULL);
  free (kwset);
}
