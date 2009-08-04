/* mpn_powm -- Compute R = U^E mod M.

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

   * How do we handle small bases?

   * This is slower than old mpz code, in particular if we base it on redc_1
     (use: #undef HAVE_NATIVE_mpn_addmul_2).  Why?

   * Make it sub-quadratic.

   * Call new division functions, not mpn_tdiv_qr.

   * Is redc obsolete with improved SB division?

   * Consider special code for one-limb M.

   * CRT for N = odd*2^t:
      Using Newton's method and 2-adic arithmetic:
        m1_inv_m2 = 1/odd mod 2^t
      Plain 2-adic (REDC) modexp:
        r1 = a ^ b mod odd
      Mullo+sqrlo-based modexp:
        r2 = a ^ b mod 2^t
      mullo, mul, add:
        r = ((r2 - r1) * m1_i_m2 mod 2^t) * odd + r1

   * How should we handle the redc1/redc2/redc2/redc4/redc_subquad choice?
     - redc1: T(binvert_1limb)  + e * (n)   * (T(mullo1x1) + n*T(addmul_1))
     - redc2: T(binvert_2limbs) + e * (n/2) * (T(mullo2x2) + n*T(addmul_2))
     - redc3: T(binvert_3limbs) + e * (n/3) * (T(mullo3x3) + n*T(addmul_3))
     This disregards the addmul_N constant term, but we could think of
     that as part of the respective mulloNxN.
*/

#include "gmp.h"
#include "gmp-impl.h"
#include "longlong.h"


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
  static unsigned long x[] = {1,7,25,81,241,673,1793,4609,11521,28161,~0ul};
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
mpn_powm (mp_ptr rp, mp_srcptr bp, mp_size_t bn,
	  mp_srcptr ep, mp_size_t en,
	  mp_srcptr mp, mp_size_t n, mp_ptr tp)
{
  mp_limb_t mip[2];
  int cnt;
  long ebi;
  int windowsize, this_windowsize;
  mp_limb_t expbits;
  mp_ptr pp, this_pp, last_pp;
  mp_ptr b2p;
  long i;
  int redc_x;
  TMP_DECL;

  ASSERT (en > 1 || (en == 1 && ep[0] > 1));
  ASSERT (n >= 1 && ((mp[0] & 1) != 0));

  TMP_MARK;

  count_leading_zeros (cnt, ep[en - 1]);
  ebi = en * GMP_LIMB_BITS - cnt;

#if 0
  if (bn < n)
    {
      /* Do the first few exponent bits without mod reductions,
	 until the result is greater than the mod argument.  */
      for (;;)
	{
	  mpn_sqr_n (tp, this_pp, tn);
	  tn = tn * 2 - 1,  tn += tp[tn] != 0;
	  if (getbit (ep, ebi) != 0)
	    mpn_mul (..., tp, tn, bp, bn);
	  ebi--;
	}
    }
#endif

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

  pp = TMP_ALLOC_LIMBS (n << (windowsize - 1));

  this_pp = pp;
  redcify (this_pp, bp, bn, mp, n);

  b2p = tp + 2*n;

  /* Store b^2 in b2.  */
  mpn_sqr_n (tp, this_pp, n);
  MPN_REDC_X (b2p, tp, mp, n, mip);

  /* Precompute odd powers of b and put them in the temporary area at pp.  */
  for (i = (1 << (windowsize - 1)) - 1; i > 0; i--)
    {
      last_pp = this_pp;
      this_pp += n;
      mpn_mul_n (tp, last_pp, b2p, n);
      MPN_REDC_X (this_pp, tp, mp, n, mip);
    }

  expbits = getbits (ep, ebi, windowsize);
  ebi -= windowsize;
  if (ebi < 0)
    ebi = 0;

  count_trailing_zeros (cnt, expbits);
  ebi += cnt;
  expbits >>= cnt;

  MPN_COPY (rp, pp + n * (expbits >> 1), n);

  while (ebi != 0)
    {
      while (getbit (ep, ebi) == 0)
	{
	  mpn_sqr_n (tp, rp, n);
	  MPN_REDC_X (rp, tp, mp, n, mip);
	  ebi--;
	  if (ebi == 0)
	    goto done;
	}

      /* The next bit of the exponent is 1.  Now extract the largest block of
	 bits <= windowsize, and such that the least significant bit is 1.  */

      expbits = getbits (ep, ebi, windowsize);
      ebi -= windowsize;
      this_windowsize = windowsize;
      if (ebi < 0)
	{
	  this_windowsize += ebi;
	  ebi = 0;
	}

      count_trailing_zeros (cnt, expbits);
      this_windowsize -= cnt;
      ebi += cnt;
      expbits >>= cnt;

      do
	{
	  mpn_sqr_n (tp, rp, n);
	  MPN_REDC_X (rp, tp, mp, n, mip);
	  this_windowsize--;
	}
      while (this_windowsize != 0);

      mpn_mul_n (tp, rp, pp + n * (expbits >> 1), n);
      MPN_REDC_X (rp, tp, mp, n, mip);
    }

 done:
  MPN_COPY (tp, rp, n);
  MPN_ZERO (tp + n, n);
  MPN_REDC_X (rp, tp, mp, n, mip);
  if (mpn_cmp (rp, mp, n) >= 0)
    mpn_sub_n (rp, rp, mp, n);
  TMP_FREE;
}
