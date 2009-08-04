/* mpn_toom_interpolate_7pts -- Interpolate for toom44, 53, 62.

   Contributed to the GNU project by Niels Möller.

   THE FUNCTION IN THIS FILE IS INTERNAL WITH A MUTABLE INTERFACE.  IT IS ONLY
   SAFE TO REACH IT THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS ALMOST
   GUARANTEED THAT IT WILL CHANGE OR DISAPPEAR IN A FUTURE GNU MP RELEASE.

Copyright 2006, 2007, 2009 Free Software Foundation, Inc.

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

/* Arithmetic right shift, requiring that the shifted out bits are zero. */
static inline void
divexact_2exp (mp_ptr rp, mp_srcptr sp, mp_size_t n, unsigned shift)
{
  mp_limb_t sign;
  sign = LIMB_HIGHBIT_TO_MASK (sp[n-1] << GMP_NAIL_BITS) << (GMP_NUMB_BITS - shift);
  ASSERT_NOCARRY (mpn_rshift (rp, sp, n, shift));
  rp[n-1] |= sign & GMP_NUMB_MASK;
}

/* For odd divisors, mpn_divexact_1 works fine with two's complement. */
#ifndef mpn_divexact_by3
#define mpn_divexact_by3(dst,src,size) mpn_divexact_1(dst,src,size,3)
#endif
#ifndef mpn_divexact_by9
#define mpn_divexact_by9(dst,src,size) mpn_divexact_1(dst,src,size,9)
#endif
#ifndef mpn_divexact_by15
#define mpn_divexact_by15(dst,src,size) mpn_divexact_1(dst,src,size,15)
#endif

/* Interpolation for toom4, using the evaluation points infinity, 2,
   1, -1, 1/2, -1/2. More precisely, we want to compute
   f(2^(GMP_NUMB_BITS * n)) for a polynomial f of degree 6, given the
   seven values

     w0 = f(0),
     w1 = 64 f(-1/2),
     w2 = 64 f(1/2),
     w3 = f(-1),
     w4 = f(1)
     w5 = f(2)
     w6 = limit at infinity of f(x) / x^6,

   The result is 6*n + w6n limbs. At entry, w0 is stored at {rp, 2n },
   w2 is stored at { rp + 2n, 2n+1 }, and w6 is stored at { rp + 6n,
   w6n }. The other values are 2n + 1 limbs each (with most
   significant limbs small). f(-1) and f(-1/2) may be negative, signs
   determined by the flag bits. All intermediate results are
   represented in two's complement. Inputs are destroyed.

   Needs (2*n + 1) limbs of temporary storage.
*/

void
mpn_toom_interpolate_7pts (mp_ptr rp, mp_size_t n, enum toom4_flags flags,
			   mp_ptr w1, mp_ptr w3, mp_ptr w4, mp_ptr w5,
			   mp_size_t w6n, mp_ptr tp)
{
  mp_size_t m = 2*n + 1;
  mp_ptr w2 = rp + 2*n;
  mp_ptr w6 = rp + 6*n;
  mp_limb_t cy;

  ASSERT (w6n > 0);
  ASSERT (w6n <= 2*n);

  /* Using Marco Bodrato's formulas

     W5 = W5 + W2
     W3 =(W3 + W4)/2
     W1 = W1 + W2
     W2 = W2 - W6 - W0*64
     W2 =(W2*2 - W1)/8
     W4 = W4 - W3

     W5 = W5 - W4*65
     W4 = W4 - W6 - W0
     W5 = W5 + W4*45
     W2 =(W2 - W4)/3
     W4 = W4 - W2

     W1 = W1 - W5
     W5 =(W5 - W3*16)/ 18
     W3 = W3 - W5
     W1 =(W1/30 + W5)/ 2
     W5 = W5 - W1

     where W0 = f(0), W1 = 64 f(-1/2), W2 = 64 f(1/2), W3 = f(-1),
	   W4 = f(1), W5 = f(2), W6 = f(oo),
  */

  mpn_add_n (w5, w5, w2, m);
  if (flags & toom4_w3_neg)
    mpn_add_n (w3, w3, w4, m);
  else
    mpn_sub_n (w3, w4, w3, m);
  divexact_2exp (w3, w3, m, 1);
  if (flags & toom4_w1_neg)
    mpn_add_n (w1, w1, w2, m);
  else
    mpn_sub_n (w1, w2, w1, m);
  mpn_sub (w2, w2, m, w6, w6n);
  tp[2*n] = mpn_lshift (tp, rp, 2*n, 6);
  mpn_sub_n (w2, w2, tp, m);
  mpn_lshift (w2, w2, m, 1);
  mpn_sub_n (w2, w2, w1, m);
  divexact_2exp (w2, w2, m, 3);
  mpn_sub_n (w4, w4, w3, m);

  mpn_submul_1 (w5, w4, m, 65);
  mpn_sub (w4, w4, m, w6, w6n);
  mpn_sub (w4, w4, m, rp, 2*n);
  mpn_addmul_1 (w5, w4, m, 45);
  mpn_sub_n (w2, w2, w4, m);
  /* Rely on divexact working with two's complement */
  mpn_divexact_by3 (w2, w2, m);
  mpn_sub_n (w4, w4, w2, m);

  mpn_sub_n (w1, w1, w5, m);
  mpn_lshift (tp, w3, m, 4);
  mpn_sub_n (w5, w5, tp, m);
  divexact_2exp (w5, w5, m, 1);
  mpn_divexact_by9 (w5, w5, m);
  mpn_sub_n (w3, w3, w5, m);
  divexact_2exp (w1, w1, m, 1);
  mpn_divexact_by15 (w1, w1, m);
  mpn_add_n (w1, w1, w5, m);
  divexact_2exp (w1, w1, m, 1);
  mpn_sub_n (w5, w5, w1, m);

  /* Two's complement coefficients must be non-negative at the end of
     this procedure. */
  ASSERT ( !(w1[2*n] & GMP_LIMB_HIGHBIT));
  ASSERT ( !(w2[2*n] & GMP_LIMB_HIGHBIT));
  ASSERT ( !(w3[2*n] & GMP_LIMB_HIGHBIT));
  ASSERT ( !(w4[2*n] & GMP_LIMB_HIGHBIT));
  ASSERT ( !(w5[2*n] & GMP_LIMB_HIGHBIT));

  /* Addition chain. Note carries and the 2n'th limbs that need to be
   * added in.
   *
   * Special care is needed for w2[2n] and the corresponding carry,
   * since the "simple" way of adding it all together would overwrite
   * the limb at wp[2*n] and rp[4*n] (same location) with the sum of
   * the high half of w3 and the low half of w4.
   *
   *         7    6    5    4    3    2    1    0
   *    |    |    |    |    |    |    |    |    |
   *                  ||w3 (2n+1)|
   *             ||w4 (2n+1)|
   *        ||w5 (2n+1)|        ||w1 (2n+1)|
   *  + | w6 (w6n)|        ||w2 (2n+1)| w0 (2n) |  (share storage with r)
   *  -----------------------------------------------
   *  r |    |    |    |    |    |    |    |    |
   *        c7   c6   c5   c4   c3                 Carries to propagate
   */

  cy = mpn_add_n (rp + n, rp + n, w1, 2*n);
  MPN_INCR_U (w2 + n, n + 1, w1[2*n] + cy);
  cy = mpn_add_n (rp + 3*n, rp + 3*n, w3, n);
  MPN_INCR_U (w3 + n, n + 1, w2[2*n] + cy);
  cy = mpn_add_n (rp + 4*n, w3 + n, w4, n);
  MPN_INCR_U (w4 + n, n + 1, w3[2*n] + cy);
  cy = mpn_add_n (rp + 5*n, w4 + n, w5, n);
  MPN_INCR_U (w5 + n, n + 1, w4[2*n] + cy);
  if (w6n > n + 1)
    {
      mp_limb_t c7 = mpn_add_n (rp + 6*n, rp + 6*n, w5 + n, n + 1);
      MPN_INCR_U (rp + 7*n + 1, w6n - n - 1, c7);
    }
  else
    {
      ASSERT_NOCARRY (mpn_add_n (rp + 6*n, rp + 6*n, w5 + n, w6n));
#if WANT_ASSERT
      {
	mp_size_t i;
	for (i = w6n; i <= n; i++)
	  ASSERT (w5[n + i] == 0);
      }
#endif
    }
}
