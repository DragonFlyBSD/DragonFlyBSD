/* mpn_powm_sec -- Compute R = U^E mod M.  Safe variant, not leaking time info.

Copyright 2007, 2008, 2009 Free Software Foundation, Inc.

This file is part of the GNU MP Library.

The GNU MP Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the GNU MP Library.  If not, see http://www.gnu.org/licenses/.  */


/*
  BASIC ALGORITHM, Compute b^e mod n, where n is odd.

  1. w <- b

  2. While w^2 < n (and there are more bits in e)
       w <- power left-to-right base-2 without reduction

  3. t <- (B^n * b) / n                Convert to REDC form

  4. Compute power table of e-dependent size

  5. While there are more bits in e
       w <- power left-to-right base-k with reduction


  TODO:

   * Make getbits a macro, thereby allowing it to update the index operand.
     That will simplify the code using getbits.  (Perhaps make getbits' sibling
     getbit then have similar form, for symmetry.)

   * Write an itch function.

   * Choose window size without looping.  (Superoptimize or think(tm).)

   * Make it sub-quadratic.

   * Call new division functions, not mpn_tdiv_qr.

   * Is redc obsolete with improved SB division?

   * Consider special code for one-limb M.

   * Handle even M (in mpz_powm_sec) with two modexps and CRT.
*/

#include "gmp.h"
#include "gmp-impl.h"
#include "longlong.h"

#define WANT_CACHE_SECURITY 1


#define getbit(p,bi) \
  ((p[(bi - 1) / GMP_LIMB_BITS] >> (bi - 1) % GMP_LIMB_BITS) & 1)

static inline mp_limb_t
getbits (const mp_limb_t *p, unsigned long bi, int nbits)
{
  int nbits_in_r;
  mp_limb_t r;
  mp_size_t i;

  if (bi < nbits)
    {
      return p[0] & (((mp_limb_t) 1 << bi) - 1);
    }
  else
    {
      bi -= nbits;			/* bit index of low bit to extract */
      i = bi / GMP_LIMB_BITS;		/* word index of low bit to extract */
      bi %= GMP_LIMB_BITS;		/* bit index in low word */
      r = p[i] >> bi;			/* extract (low) bits */
      nbits_in_r = GMP_LIMB_BITS - bi;	/* number of bits now in r */
      if (nbits_in_r < nbits)		/* did we get enough bits? */
	r += p[i + 1] << nbits_in_r;	/* prepend bits from higher word */
      return r & (((mp_limb_t ) 1 << nbits) - 1);
    }
}

#undef HAVE_NATIVE_mpn_addmul_2

#ifndef HAVE_NATIVE_mpn_addmul_2
#define REDC_2_THRESHOLD		MP_SIZE_T_MAX
#endif

#ifndef REDC_2_THRESHOLD
#define REDC_2_THRESHOLD		4
#endif

static void mpn_redc_n () {ASSERT_ALWAYS(0);}

static inline int
win_size (unsigned long eb)
{
  int k;
  static unsigned long x[] = {1,4,27,100,325,1026,2905,7848,20457,51670,~0ul};
  for (k = 0; eb > x[k]; k++)
    ;
  return k;
}

#define MPN_REDC_X(rp, tp, mp, n, mip)					\
  do {									\
    if (redc_x == 1)							\
      mpn_redc_1 (rp, tp, mp, n, mip[0]);				\
    else if (redc_x == 2)						\
      mpn_redc_2 (rp, tp, mp, n, mip);					\
    else								\
      mpn_redc_n (rp, tp, mp, n, mip);					\
  } while (0)

  /* Convert U to REDC form, U_r = B^n * U mod M */
static void
redcify (mp_ptr rp, mp_srcptr up, mp_size_t un, mp_srcptr mp, mp_size_t n)
{
  mp_ptr tp, qp;
  TMP_DECL;
  TMP_MARK;

  tp = TMP_ALLOC_LIMBS (un + n);
  qp = TMP_ALLOC_LIMBS (un + 1);	/* FIXME: Put at tp+? */

  MPN_ZERO (tp, n);
  MPN_COPY (tp + n, up, un);
  mpn_tdiv_qr (qp, rp, 0L, tp, un + n, mp, n);
  TMP_FREE;
}

/* rp[n-1..0] = bp[bn-1..0] ^ ep[en-1..0] mod mp[n-1..0]
   Requires that mp[n-1..0] is odd.
   Requires that ep[en-1..0] is > 1.
   Uses scratch space tp[3n..0], i.e., 3n+1 words.  */
void
mpn_powm_sec (mp_ptr rp, mp_srcptr bp, mp_size_t bn,
	      mp_srcptr ep, mp_size_t en,
	      mp_srcptr mp, mp_size_t n, mp_ptr tp)
{
  mp_limb_t mip[2];
  int cnt;
  long ebi;
  int windowsize, this_windowsize;
  mp_limb_t expbits;
  mp_ptr pp, this_pp, last_pp;
  long i;
  int redc_x;
  TMP_DECL;

  ASSERT (en > 1 || (en == 1 && ep[0] > 1));
  ASSERT (n >= 1 && ((mp[0] & 1) != 0));

  TMP_MARK;

  count_leading_zeros (cnt, ep[en - 1]);
  ebi = en * GMP_LIMB_BITS - cnt;

  windowsize = win_size (ebi);

  if (BELOW_THRESHOLD (n, REDC_2_THRESHOLD))
    {
      binvert_limb (mip[0], mp[0]);
      mip[0] = -mip[0];
      redc_x = 1;
    }
#if defined (HAVE_NATIVE_mpn_addmul_2)
  else
    {
      mpn_binvert (mip, mp, 2, tp);
      mip[0] = -mip[0]; mip[1] = ~mip[1];
      redc_x = 2;
    }
#endif
#if 0
  mpn_binvert (mip, mp, n, tp);
  redc_x = 0;
#endif

  pp = TMP_ALLOC_LIMBS (n << windowsize);

  this_pp = pp;
  this_pp[n] = 1;
  redcify (this_pp, this_pp + n, 1, mp, n);
  this_pp += n;
  redcify (this_pp, bp, bn, mp, n);

  /* Precompute powers of b and put them in the temporary area at pp.  */
  for (i = (1 << windowsize) - 2; i > 0; i--)
    {
      last_pp = this_pp;
      this_pp += n;
      mpn_mul_n (tp, last_pp, pp + n, n);
      MPN_REDC_X (this_pp, tp, mp, n, mip);
    }

  expbits = getbits (ep, ebi, windowsize);
  ebi -= windowsize;
  if (ebi < 0)
    ebi = 0;

  MPN_COPY (rp, pp + n * expbits, n);

  while (ebi != 0)
    {
      expbits = getbits (ep, ebi, windowsize);
      ebi -= windowsize;
      this_windowsize = windowsize;
      if (ebi < 0)
	{
	  this_windowsize += ebi;
	  ebi = 0;
	}

      do
	{
	  mpn_sqr_n (tp, rp, n);
	  MPN_REDC_X (rp, tp, mp, n, mip);
	  this_windowsize--;
	}
      while (this_windowsize != 0);

#if WANT_CACHE_SECURITY
      mpn_tabselect (tp + 2*n, pp, n, 1 << windowsize, expbits);
      mpn_mul_n (tp, rp, tp + 2*n, n);
#else
      mpn_mul_n (tp, rp, pp + n * expbits, n);
#endif
      MPN_REDC_X (rp, tp, mp, n, mip);
    }

  MPN_COPY (tp, rp, n);
  MPN_ZERO (tp + n, n);
  MPN_REDC_X (rp, tp, mp, n, mip);
  if (mpn_cmp (rp, mp, n) >= 0)
    mpn_sub_n (rp, rp, mp, n);
  TMP_FREE;
}

#if ! HAVE_NATIVE_mpn_tabselect
/* Select entry `which' from table `tab', which has nents entries, each `n'
   limbs.  Store the selected entry at rp.  Reads entire table to avoid
   sideband information leaks.  O(n*nents).  */

void
mpn_tabselect (volatile mp_limb_t *rp, volatile mp_limb_t *tab, mp_size_t n,
	       mp_size_t nents, mp_size_t which)
{
  mp_size_t k, i;
  mp_limb_t mask;
  volatile mp_limb_t *tp;

  for (k = 0; k < nents; k++)
    {
      mask = -(mp_limb_t) (which == k);
      tp = tab + n * k;
      for (i = 0; i < n; i++)
	{
	  rp[i] = (rp[i] & ~mask) | (tp[i] & mask);
	}
    }
}
#endif
