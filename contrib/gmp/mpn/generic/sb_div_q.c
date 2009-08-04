/* mpn_sb_div_q -- schoolbook division with 2-limb sloppy non-greater
   precomputed inverse, returning an accurate quotient.

   Contributed to the GNU project by Torbjörn Granlund.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL WITH A MUTABLE INTERFACE.  IT IS
   ONLY SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS
   ALMOST GUARANTEED THAT THEY WILL CHANGE OR DISAPPEAR IN A FUTURE GMP
   RELEASE.

Copyright 2006, 2007 Free Software Foundation, Inc.

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

/*
  CAVEATS:
  1. Should it demand normalized operands like now, or normalize on-the-fly?
  2. Overwrites {np,nn}.
  3. Uses mpn_submul_1.  It would be nice to somehow make it use mpn_addmul_1
     instead.  (That would open for mpn_addmul_2 straightforwardly.)
*/

mp_limb_t
mpn_sb_div_q (mp_ptr qp,
	      mp_ptr np, mp_size_t nn,
	      mp_srcptr dp, mp_size_t dn,
	      mp_srcptr dip)
{
  mp_limb_t q, q10, q01a, q00a, q01b, q00b;
  mp_limb_t cy;
  mp_size_t i;
  mp_limb_t qh;
  mp_limb_t di1, di0;
  mp_size_t qn;

  mp_size_t dn_orig = dn;
  mp_srcptr dp_orig = dp;
  mp_ptr np_orig = np;

  ASSERT (dn > 0);
  ASSERT (nn >= dn);
  ASSERT ((dp[dn-1] & GMP_NUMB_HIGHBIT) != 0);
  ASSERT (! MPN_OVERLAP_P (np, nn, dp, dn));
  ASSERT (! MPN_OVERLAP_P (qp, nn-dn, dp, dn));
  ASSERT (! MPN_OVERLAP_P (qp, nn-dn, np, nn) || qp+dn >= np);
  ASSERT_MPN (np, nn);
  ASSERT_MPN (dp, dn);

  np += nn;
  qn = nn - dn;
  if (qn + 1 < dn)
    {
      dp += dn - (qn + 1);
      dn = qn + 1;
    }

  qh = mpn_cmp (np - dn, dp, dn) >= 0;
  if (qh != 0)
    mpn_sub_n (np - dn, np - dn, dp, dn);

  qp += qn;
  di1 = dip[1]; di0 = dip[0];
  for (i = qn; i >= dn; i--)
    {
      np--;
      umul_ppmm (q, q10, np[0], di1);
      umul_ppmm (q01a, q00a, np[-1], di1);
      add_ssaaaa (q, q10, q, q10, np[0], q01a);
      umul_ppmm (q01b, q00b, np[0], di0);
      add_ssaaaa (q, q10, q, q10, 0, q01b);
      add_ssaaaa (q, q10, q, q10, 0, np[-1]);

      cy = mpn_submul_1 (np - dn, dp, dn, q);

      if (UNLIKELY (np[0] > cy || mpn_cmp (np - dn, dp, dn) >= 0))
	{
	  q = q + 1;
	  mpn_sub_n (np - dn, np - dn, dp, dn);
	}

      *--qp = q;
    }

  for (i = dn - 1; i > 0; i--)
    {
      np--;
      umul_ppmm (q, q10, np[0], di1);
      umul_ppmm (q01a, q00a, np[-1], di1);
      add_ssaaaa (q, q10, q, q10, np[0], q01a);
      umul_ppmm (q01b, q00b, np[0], di0);
      add_ssaaaa (q, q10, q, q10, 0, q01b);
      add_ssaaaa (q, q10, q, q10, 0, np[-1]);

      cy = mpn_submul_1 (np - dn, dp, dn, q);

      if (UNLIKELY (np[0] > cy || mpn_cmp (np - dn, dp, dn) >= 0))
	{
	  q = q + 1;
	  if (q == 0)
	    q = GMP_NUMB_MAX;
	  else
	    mpn_sub_n (np - dn, np - dn, dp, dn);
	}

      *--qp = q;

      /* Truncate operands.  */
      dn--;
      dp++;

      /* The partial remainder might be equal to the truncated divisor,
	 thus non-canonical.  When that happens, the rest of the quotient
	 should be all ones.  */
      if (UNLIKELY (mpn_cmp (np - dn, dp, dn) == 0))
	{
	  while (--i)
	    *--qp = GMP_NUMB_MAX;
	  break;
	}
    }

  dn = dn_orig;
  if (UNLIKELY (np[-1] < dn))
    {
      mp_limb_t q, x;

      /* The quotient may be too large if the remainder is small.  Recompute
	 for above ignored operand parts, until the remainder spills.

	 FIXME: The quality of this code isn't the same as the code above.
	 1. We don't compute things in an optimal order, high-to-low, in order
	    to terminate as quickly as possible.
	 2. We mess with pointers and sizes, adding and subtracting and
	    adjusting to get things right.  It surely could be streamlined.
	 3. The only termination criteria are that we determine that the
	    quotient needs to be adjusted, or that we have recomputed
	    everything.  We should stop when the remainder is so large
	    that no additional subtracting could make it spill.
	 4. If nothing else, we should not do two loops of submul_1 over the
	    data, instead handle both the triangularization and chopping at
	    once.  */

      x = np[-1];

      if (dn > 2)
	{
	  /* Compensate for triangularization.  */
	  mp_limb_t y;

	  dp = dp_orig;
	  if (qn + 1 < dn)
	    {
	      dp += dn - (qn + 1);
	      dn = qn + 1;
	    }

	  y = np[-2];

	  for (i = dn - 3; i >= 0; i--)
	    {
	      q = qp[i];
	      cy = mpn_submul_1 (np - (dn - i), dp, dn - i - 2, q);

	      if (y < cy)
		{
		  if (x == 0)
		    {
		      cy = mpn_sub_1 (qp, qp, qn, 1);
		      ASSERT_ALWAYS (cy == 0);
		      return qh - cy;
		    }
		  x--;
		}
	      y -= cy;
	    }
	  np[-2] = y;
	}

      dn = dn_orig;
      if (qn + 1 < dn)
	{
	  /* Compensate for ignored dividend and divisor tails.  */

	  if (qn == 0)
	    return qh;

	  dp = dp_orig;
	  np = np_orig;

	  if (qh != 0)
	    {
	      cy = mpn_sub_n (np + qn, np + qn, dp, dn - (qn + 1));
	      if (cy != 0)
		{
		  if (x == 0)
		    {
		      cy = mpn_sub_1 (qp, qp, qn, 1);
		      return qh - cy;
		    }
		  x--;
		}
	    }

	  for (i = dn - qn - 2; i >= 0; i--)
	    {
	      cy = mpn_submul_1 (np + i, qp, qn, dp[i]);
	      cy = mpn_sub_1 (np + qn + i, np + qn + i, dn - qn - i - 1, cy);
	      if (cy != 0)
		{
		  if (x == 0)
		    {
		      cy = mpn_sub_1 (qp, qp, qn, 1);
		      ASSERT_ALWAYS (cy == 0);
		      return qh - cy;
		    }
		  x--;
		}
	    }
	}
    }

  return qh;
}
