/* mpn_sb_bdiv_q -- schoolbook Hensel division with precomputed inverse,
   returning quotient only.

   Contributed to the GNU project by Niels Möller.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL FUNCTIONS WITH MUTABLE INTERFACES.
   IT IS ONLY SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS
   ALMOST GUARANTEED THAT THEY'LL CHANGE OR DISAPPEAR IN A FUTURE GMP RELEASE.

Copyright 2005, 2006 Free Software Foundation, Inc.

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


/* Computes Q = N / D mod B^nn, destroys N.
   Clobbers N.

   D must be odd. dinv is (-D)^-1 mod B.


   The straightforward way to compute Q is to cancel one limb at a time, using

     qp[i] = D^{-1} * np[i] (mod B)
     N -= B^i * qp[i] * D

   But we prefer addition to subtraction, since mpn_addmul_1 is often faster
   than mpn_submul_1.  Q = - N / D can be computed by iterating

     qp[i] = (-D)^{-1} * np[i] (mod B)
     N += B^i * qp[i] * D

   And then we flip the sign, -Q = (not Q) + 1.
*/

void
mpn_sb_bdiv_q (mp_ptr qp,
	       mp_ptr np, mp_size_t nn,
	       mp_srcptr dp, mp_size_t dn,
	       mp_limb_t dinv)
{
  mp_size_t i;
  mp_limb_t qh;

  ASSERT (nn > 0);
  ASSERT (dn > 0);
  ASSERT (nn >= dn);
  ASSERT (dp[0] & 1);

  for (i = 0; i < nn - dn; i++)
    {
      mp_limb_t cy;
      mp_limb_t q;

      q = dinv * np[i];
      qp[i] = ~q;
      cy = mpn_addmul_1 (np + i, dp, dn, q);
      mpn_add_1 (np + i + dn, np + i + dn, nn - i - dn, cy);
      ASSERT (np[i] == 0);
    }

  for (; i < nn - 1; i++)
    {
      mp_limb_t q;

      q = dinv * np[i];
      qp[i] = ~q;
      mpn_addmul_1 (np + i, dp, nn - i, q);

      ASSERT (np[i] == 0);
    }

  /* Final limb */
  qp[nn - 1] = ~(dinv * np[nn - 1]);
  qh = mpn_add_1 (qp, qp, nn, 1); /* FIXME: can we get carry? */
}
