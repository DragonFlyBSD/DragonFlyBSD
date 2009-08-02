/* mpn_powlo -- Compute R = U^E mod R^n, where R is the limb base.

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

static inline int
win_size (unsigned long eb)
{
  int k;
  static unsigned long x[] = {1,7,25,81,241,673,1793,4609,11521,28161,~0ul};
  for (k = 0; eb > x[k]; k++)
    ;
  return k;
}

/* rp[n-1..0] = bp[n-1..0] ^ ep[en-1..0] mod R^n, R is the limb base.
   Requires that ep[en-1] is non-zero.
   Uses scratch space tp[3n-1..0], i.e., 3n words.  */
void
mpn_powlo (mp_ptr rp, mp_srcptr bp,
	   mp_srcptr ep, mp_size_t en,
	   mp_size_t n, mp_ptr tp)
{
  int cnt;
  long ebi;
  int windowsize, this_windowsize;
  mp_limb_t expbits;
  mp_limb_t *pp, *this_pp, *last_pp;
  mp_limb_t *b2p;
  long i;
  TMP_DECL;

  ASSERT (en > 1 || (en == 1 && ep[0] > 1));

  TMP_MARK;

  count_leading_zeros (cnt, ep[en - 1]);
  ebi = en * GMP_LIMB_BITS - cnt;

  windowsize = win_size (ebi);

  pp = TMP_ALLOC_LIMBS ((n << (windowsize - 1)) + n); /* + n is for mullow ign part */

  this_pp = pp;

  MPN_COPY (this_pp, bp, n);

  b2p = tp + 2*n;

  /* Store b^2 in b2.  */
  mpn_sqr_n (tp, bp, n);	/* FIXME: Use "mpn_sqrlo" */
  MPN_COPY (b2p, tp, n);

  /* Precompute odd powers of b and put them in the temporary area at pp.  */
  for (i = (1 << (windowsize - 1)) - 1; i > 0; i--)
    {
      last_pp = this_pp;
      this_pp += n;
      mpn_mullow_n (this_pp, last_pp, b2p, n);
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
	  mpn_sqr_n (tp, rp, n);	/* FIXME: Use "mpn_sqrlo" */
	  MPN_COPY (rp, tp, n);
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
	  MPN_COPY (rp, tp, n);
	  this_windowsize--;
	}
      while (this_windowsize != 0);

      mpn_mullow_n (tp, rp, pp + n * (expbits >> 1), n);
      MPN_COPY (rp, tp, n);
    }

 done:
  TMP_FREE;
}
