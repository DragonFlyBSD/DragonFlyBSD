/* matrix22_mul.c.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL WITH MUTABLE INTERFACES.  IT IS ONLY
   SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS ALMOST
   GUARANTEED THAT THEY'LL CHANGE OR DISAPPEAR IN A FUTURE GNU MP RELEASE.

Copyright 2003, 2004, 2005, 2008 Free Software Foundation, Inc.

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

#define MUL(rp, ap, an, bp, bn) do {		\
  if (an >= bn)					\
    mpn_mul (rp, ap, an, bp, bn);		\
  else						\
    mpn_mul (rp, bp, bn, ap, an);		\
} while (0)

/* Inputs are unsigned. */
static int
abs_sub_n (mp_ptr rp, mp_srcptr ap, mp_srcptr bp, mp_size_t n)
{
  int c;
  MPN_CMP (c, ap, bp, n);
  if (c >= 0)
    {
      mpn_sub_n (rp, ap, bp, n);
      return 0;
    }
  else
    {
      mpn_sub_n (rp, bp, ap, n);
      return 1;
    }
}

static int
add_signed_n (mp_ptr rp,
	      mp_srcptr ap, int as, mp_srcptr bp, int bs, mp_size_t n)
{
  if (as != bs)
    return as ^ abs_sub_n (rp, ap, bp, n);
  else
    {
      ASSERT_NOCARRY (mpn_add_n (rp, ap, bp, n));
      return as;
    }
}

mp_size_t
mpn_matrix22_mul_itch (mp_size_t rn, mp_size_t mn)
{
  if (BELOW_THRESHOLD (rn, MATRIX22_STRASSEN_THRESHOLD)
      || BELOW_THRESHOLD (mn, MATRIX22_STRASSEN_THRESHOLD))
    return 3*rn + 2*mn;
  else
    return 4*(rn + mn) + 5;
}

/* Algorithm:

    / s0 \   /  1  0  0  0 \ / r0 \
    | s1 |   |  0  1  0  0 | | r1 |
    | s2 |   |  0  0  1  1 | | r2 |
    | s3 | = | -1  0  1  1 | \ r3 /
    | s4 |   |  1  0 -1  0 |
    | s5 |   |  1  1 -1 -1 |
    \ s6 /   \  0  0  0  1 /

    / t0 \   /  1  0  0  0 \ / m0 \
    | t1 |   |  0  0  1  0 | | m1 |
    | t2 |   | -1  1  0  0 | | m2 |
    | t3 | = |  1 -1  0  1 | \ m3 /
    | t4 |   |  0 -1  0  1 |
    | t5 |   |  0  0  0  1 |
    \ t6 /   \ -1  1  1 -1 /

    / r0 \   / 1 1 0 0 0 0 0 \ / s0 * t0 \
    | r1 | = | 1 0 1 1 0 1 0 | | s1 * t1 |
    | r2 |   | 1 0 0 1 1 0 1 | | s2 * t2 |
    \ r3 /   \ 1 0 1 1 1 0 0 / | s3 * t3 |
			       | s4 * t4 |
			       | s5 * t5 |
			       \ s6 * t6 /
*/

/* Computes R = R * M. Elements are numbers R = (r0, r1; r2, r3).
 *
 * Resulting elements are of size up to rn + mn + 1.
 *
 * Temporary storage: 4 rn + 4 mn + 5. */
void
mpn_matrix22_mul_strassen (mp_ptr r0, mp_ptr r1, mp_ptr r2, mp_ptr r3, mp_size_t rn,
			   mp_srcptr m0, mp_srcptr m1, mp_srcptr m2, mp_srcptr m3, mp_size_t mn,
			   mp_ptr tp)
{
  mp_ptr s2, s3, t2, t3, u0, u1;
  int r2s, r3s, s3s, t2s, t3s, u0s, u1s;
  s2 = tp; tp += rn;
  s3 = tp; tp += rn + 1;
  t2 = tp; tp += mn;
  t3 = tp; tp += mn + 1;
  u0 = tp; tp += rn + mn + 1;
  u1 = tp; /* rn + mn + 2 */

  MUL (u0, r0, rn, m0, mn); /* 0 */
  MUL (u1, r1, rn, m2, mn); /* 1 */

  MPN_COPY (s2, r3, rn);

  r3[rn] = mpn_add_n (r3, r3, r2, rn);
  r0[rn] = 0;
  s3s = abs_sub_n (s3, r3, r0, rn + 1);
  t2s = abs_sub_n (t2, m1, m0, mn);
  if (t2s)
    {
      t3[mn] = mpn_add_n (t3, m3, t2, mn);
      t3s = 0;
    }
  else
    {
      t3s = abs_sub_n (t3, m3, t2, mn);
      t3[mn] = 0;
    }

  r2s = abs_sub_n (r2, r0, r2, rn);
  r0[rn+mn] = mpn_add_n (r0, u0, u1, rn + mn);

  MUL(u1, s3, rn+1, t3, mn+1); /* 3 */
  u1s = s3s ^ t3s;
  ASSERT (u1[rn+mn+1] == 0);
  ASSERT (u1[rn+mn] < 4);

  if (u1s)
    {
      u0[rn+mn] = 0;
      u0s = abs_sub_n (u0, u0, u1, rn + mn + 1);
    }
  else
    {
      u0[rn+mn] = u1[rn+mn] + mpn_add_n (u0, u0, u1, rn + mn);
      u0s = 0;
    }
  MUL(u1, r3, rn + 1, t2, mn); /* 2 */
  u1s = t2s;
  ASSERT (u1[rn+mn] < 2);

  u1s = add_signed_n (u1, u0, u0s, u1, u1s, rn + mn + 1);

  t2s = abs_sub_n (t2, m3, m1, mn);
  if (s3s)
    {
      s3[rn] += mpn_add_n (s3, s3, r1, rn);
      s3s = 0;
    }
  else if (s3[rn] > 0)
    {
      s3[rn] -= mpn_sub_n (s3, s3, r1, rn);
      s3s = 1;
    }
  else
    {
      s3s = abs_sub_n (s3, r1, s3, rn);
    }
  MUL (r1, s3, rn+1, m3, mn); /* 5 */
  ASSERT_NOCARRY(add_signed_n (r1, r1, s3s, u1, u1s, rn + mn + 1));
  ASSERT (r1[rn + mn] < 2);

  MUL (r3, r2, rn, t2, mn); /* 4 */
  r3s = r2s ^ t2s;
  r3[rn + mn] = 0;
  u0s = add_signed_n (u0, u0, u0s, r3, r3s, rn + mn + 1);
  ASSERT_NOCARRY (add_signed_n (r3, r3, r3s, u1, u1s, rn + mn + 1));
  ASSERT (r3[rn + mn] < 2);

  if (t3s)
    {
      t3[mn] += mpn_add_n (t3, m2, t3, mn);
      t3s = 0;
    }
  else if (t3[mn] > 0)
    {
      t3[mn] -= mpn_sub_n (t3, t3, m2, mn);
      t3s = 1;
    }
  else
    {
      t3s = abs_sub_n (t3, m2, t3, mn);
    }
  MUL (r2, s2, rn, t3, mn + 1); /* 6 */

  ASSERT_NOCARRY (add_signed_n (r2, r2, t3s, u0, u0s, rn + mn + 1));
  ASSERT (r2[rn + mn] < 2);
}

void
mpn_matrix22_mul (mp_ptr r0, mp_ptr r1, mp_ptr r2, mp_ptr r3, mp_size_t rn,
		  mp_srcptr m0, mp_srcptr m1, mp_srcptr m2, mp_srcptr m3, mp_size_t mn,
		  mp_ptr tp)
{
  if (BELOW_THRESHOLD (rn, MATRIX22_STRASSEN_THRESHOLD)
      || BELOW_THRESHOLD (mn, MATRIX22_STRASSEN_THRESHOLD))
    {
      mp_ptr p0, p1;
      unsigned i;

      /* Temporary storage: 3 rn + 2 mn */
      p0 = tp + rn;
      p1 = p0 + rn + mn;

      for (i = 0; i < 2; i++)
	{
	  MPN_COPY (tp, r0, rn);

	  if (rn >= mn)
	    {
	      mpn_mul (p0, r0, rn, m0, mn);
	      mpn_mul (p1, r1, rn, m3, mn);
	      mpn_mul (r0, r1, rn, m2, mn);
	      mpn_mul (r1, tp, rn, m1, mn);
	    }
	  else
	    {
	      mpn_mul (p0, m0, mn, r0, rn);
	      mpn_mul (p1, m3, mn, r1, rn);
	      mpn_mul (r0, m2, mn, r1, rn);
	      mpn_mul (r1, m1, mn, tp, rn);
	    }
	  r0[rn+mn] = mpn_add_n (r0, r0, p0, rn + mn);
	  r1[rn+mn] = mpn_add_n (r1, r1, p1, rn + mn);

	  r0 = r2; r1 = r3;
	}
    }
  else
    mpn_matrix22_mul_strassen (r0, r1, r2, r3, rn,
			       m0, m1, m2, m3, mn, tp);
}
