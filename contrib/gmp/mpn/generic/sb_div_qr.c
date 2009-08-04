/* mpn_sb_div_qr -- schoolbook division with 2-limb sloppy non-greater
   precomputed inverse, returning quotient and remainder.

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
  2. Overwrites {np,nn} instead of writing remainder to a designated area.
  3. Uses mpn_submul_1.  It would be nice to somehow make it use mpn_addmul_1
     instead.  (That would open for mpn_addmul_2 straightforwardly.)
*/

mp_limb_t
mpn_sb_div_qr (mp_ptr qp,
	       mp_ptr np, mp_size_t nn,
	       mp_srcptr dp, mp_size_t dn,
	       mp_srcptr dip)
{
  mp_limb_t q, q10, q01a, q00a, q01b, q00b;
  mp_limb_t cy;
  mp_size_t i;
  mp_limb_t qh;
  mp_limb_t di1, di0;

  ASSERT (dn > 0);
  ASSERT (nn >= dn);
  ASSERT ((dp[dn-1] & GMP_NUMB_HIGHBIT) != 0);
  ASSERT (! MPN_OVERLAP_P (np, nn, dp, dn));
  ASSERT (! MPN_OVERLAP_P (qp, nn-dn, dp, dn));
  ASSERT (! MPN_OVERLAP_P (qp, nn-dn, np, nn) || qp+dn >= np);
  ASSERT_MPN (np, nn);
  ASSERT_MPN (dp, dn);

  np += nn;

  qh = mpn_cmp (np - dn, dp, dn) >= 0;
  if (qh != 0)
    mpn_sub_n (np - dn, np - dn, dp, dn);

  qp += nn - dn;
  di1 = dip[1]; di0 = dip[0];
  for (i = nn - dn; i > 0; i--)
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

  return qh;
}
