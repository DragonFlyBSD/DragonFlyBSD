/* mpn_mul_n and helper function -- Multiply/square natural numbers.

   THE HELPER FUNCTIONS IN THIS FILE (meaning everything except mpn_mul_n) ARE
   INTERNAL WITH MUTABLE INTERFACES.  IT IS ONLY SAFE TO REACH THEM THROUGH
   DOCUMENTED INTERFACES.  IN FACT, IT IS ALMOST GUARANTEED THAT THEY'LL CHANGE
   OR DISAPPEAR IN A FUTURE GNU MP RELEASE.

Copyright 1991, 1993, 1994, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
2005 Free Software Foundation, Inc.

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


/* Multiplies using 3 half-sized mults and so on recursively.
 * p[0..2*n-1] := product of a[0..n-1] and b[0..n-1].
 * No overlap of p[...] with a[...] or b[...].
 * ws is workspace.
 */

void
mpn_kara_mul_n (mp_ptr p, mp_srcptr a, mp_srcptr b, mp_size_t n, mp_ptr ws)
{
  mp_limb_t w, w0, w1;
  mp_size_t n2;
  mp_srcptr x, y;
  mp_size_t i;
  int sign;

  n2 = n >> 1;
  ASSERT (n2 > 0);

  if ((n & 1) != 0)
    {
      /* Odd length. */
      mp_size_t n1, n3, nm1;

      n3 = n - n2;

      sign = 0;
      w = a[n2];
      if (w != 0)
	w -= mpn_sub_n (p, a, a + n3, n2);
      else
	{
	  i = n2;
	  do
	    {
	      --i;
	      w0 = a[i];
	      w1 = a[n3 + i];
	    }
	  while (w0 == w1 && i != 0);
	  if (w0 < w1)
	    {
	      x = a + n3;
	      y = a;
	      sign = ~0;
	    }
	  else
	    {
	      x = a;
	      y = a + n3;
	    }
	  mpn_sub_n (p, x, y, n2);
	}
      p[n2] = w;

      w = b[n2];
      if (w != 0)
	w -= mpn_sub_n (p + n3, b, b + n3, n2);
      else
	{
	  i = n2;
	  do
	    {
	      --i;
	      w0 = b[i];
	      w1 = b[n3 + i];
	    }
	  while (w0 == w1 && i != 0);
	  if (w0 < w1)
	    {
	      x = b + n3;
	      y = b;
	      sign = ~sign;
	    }
	  else
	    {
	      x = b;
	      y = b + n3;
	    }
	  mpn_sub_n (p + n3, x, y, n2);
	}
      p[n] = w;

      n1 = n + 1;
      if (n2 < MUL_KARATSUBA_THRESHOLD)
	{
	  if (n3 < MUL_KARATSUBA_THRESHOLD)
	    {
	      mpn_mul_basecase (ws, p, n3, p + n3, n3);
	      mpn_mul_basecase (p, a, n3, b, n3);
	    }
	  else
	    {
	      mpn_kara_mul_n (ws, p, p + n3, n3, ws + n1);
	      mpn_kara_mul_n (p, a, b, n3, ws + n1);
	    }
	  mpn_mul_basecase (p + n1, a + n3, n2, b + n3, n2);
	}
      else
	{
	  mpn_kara_mul_n (ws, p, p + n3, n3, ws + n1);
	  mpn_kara_mul_n (p, a, b, n3, ws + n1);
	  mpn_kara_mul_n (p + n1, a + n3, b + n3, n2, ws + n1);
	}

      if (sign)
	mpn_add_n (ws, p, ws, n1);
      else
	mpn_sub_n (ws, p, ws, n1);

      nm1 = n - 1;
      if (mpn_add_n (ws, p + n1, ws, nm1))
	{
	  mp_limb_t x = (ws[nm1] + 1) & GMP_NUMB_MASK;
	  ws[nm1] = x;
	  if (x == 0)
	    ws[n] = (ws[n] + 1) & GMP_NUMB_MASK;
	}
      if (mpn_add_n (p + n3, p + n3, ws, n1))
	{
	  mpn_incr_u (p + n1 + n3, 1);
	}
    }
  else
    {
      /* Even length. */
      i = n2;
      do
	{
	  --i;
	  w0 = a[i];
	  w1 = a[n2 + i];
	}
      while (w0 == w1 && i != 0);
      sign = 0;
      if (w0 < w1)
	{
	  x = a + n2;
	  y = a;
	  sign = ~0;
	}
      else
	{
	  x = a;
	  y = a + n2;
	}
      mpn_sub_n (p, x, y, n2);

      i = n2;
      do
	{
	  --i;
	  w0 = b[i];
	  w1 = b[n2 + i];
	}
      while (w0 == w1 && i != 0);
      if (w0 < w1)
	{
	  x = b + n2;
	  y = b;
	  sign = ~sign;
	}
      else
	{
	  x = b;
	  y = b + n2;
	}
      mpn_sub_n (p + n2, x, y, n2);

      /* Pointwise products. */
      if (n2 < MUL_KARATSUBA_THRESHOLD)
	{
	  mpn_mul_basecase (ws, p, n2, p + n2, n2);
	  mpn_mul_basecase (p, a, n2, b, n2);
	  mpn_mul_basecase (p + n, a + n2, n2, b + n2, n2);
	}
      else
	{
	  mpn_kara_mul_n (ws, p, p + n2, n2, ws + n);
	  mpn_kara_mul_n (p, a, b, n2, ws + n);
	  mpn_kara_mul_n (p + n, a + n2, b + n2, n2, ws + n);
	}

      /* Interpolate. */
      if (sign)
	w = mpn_add_n (ws, p, ws, n);
      else
	w = -mpn_sub_n (ws, p, ws, n);
      w += mpn_add_n (ws, p + n, ws, n);
      w += mpn_add_n (p + n2, p + n2, ws, n);
      MPN_INCR_U (p + n2 + n, 2 * n - (n2 + n), w);
    }
}

void
mpn_kara_sqr_n (mp_ptr p, mp_srcptr a, mp_size_t n, mp_ptr ws)
{
  mp_limb_t w, w0, w1;
  mp_size_t n2;
  mp_srcptr x, y;
  mp_size_t i;

  n2 = n >> 1;
  ASSERT (n2 > 0);

  if ((n & 1) != 0)
    {
      /* Odd length. */
      mp_size_t n1, n3, nm1;

      n3 = n - n2;

      w = a[n2];
      if (w != 0)
	w -= mpn_sub_n (p, a, a + n3, n2);
      else
	{
	  i = n2;
	  do
	    {
	      --i;
	      w0 = a[i];
	      w1 = a[n3 + i];
	    }
	  while (w0 == w1 && i != 0);
	  if (w0 < w1)
	    {
	      x = a + n3;
	      y = a;
	    }
	  else
	    {
	      x = a;
	      y = a + n3;
	    }
	  mpn_sub_n (p, x, y, n2);
	}
      p[n2] = w;

      n1 = n + 1;

      /* n2 is always either n3 or n3-1 so maybe the two sets of tests here
	 could be combined.  But that's not important, since the tests will
	 take a miniscule amount of time compared to the function calls.  */
      if (BELOW_THRESHOLD (n3, SQR_BASECASE_THRESHOLD))
	{
	  mpn_mul_basecase (ws, p, n3, p, n3);
	  mpn_mul_basecase (p,  a, n3, a, n3);
	}
      else if (BELOW_THRESHOLD (n3, SQR_KARATSUBA_THRESHOLD))
	{
	  mpn_sqr_basecase (ws, p, n3);
	  mpn_sqr_basecase (p,  a, n3);
	}
      else
	{
	  mpn_kara_sqr_n   (ws, p, n3, ws + n1);	 /* (x-y)^2 */
	  mpn_kara_sqr_n   (p,  a, n3, ws + n1);	 /* x^2	    */
	}
      if (BELOW_THRESHOLD (n2, SQR_BASECASE_THRESHOLD))
	mpn_mul_basecase (p + n1, a + n3, n2, a + n3, n2);
      else if (BELOW_THRESHOLD (n2, SQR_KARATSUBA_THRESHOLD))
	mpn_sqr_basecase (p + n1, a + n3, n2);
      else
	mpn_kara_sqr_n   (p + n1, a + n3, n2, ws + n1);	 /* y^2	    */

      /* Since x^2+y^2-(x-y)^2 = 2xy >= 0 there's no need to track the
	 borrow from mpn_sub_n.	 If it occurs then it'll be cancelled by a
	 carry from ws[n].  Further, since 2xy fits in n1 limbs there won't
	 be any carry out of ws[n] other than cancelling that borrow. */

      mpn_sub_n (ws, p, ws, n1);	     /* x^2-(x-y)^2 */

      nm1 = n - 1;
      if (mpn_add_n (ws, p + n1, ws, nm1))   /* x^2+y^2-(x-y)^2 = 2xy */
	{
	  mp_limb_t x = (ws[nm1] + 1) & GMP_NUMB_MASK;
	  ws[nm1] = x;
	  if (x == 0)
	    ws[n] = (ws[n] + 1) & GMP_NUMB_MASK;
	}
      if (mpn_add_n (p + n3, p + n3, ws, n1))
	{
	  mpn_incr_u (p + n1 + n3, 1);
	}
    }
  else
    {
      /* Even length. */
      i = n2;
      do
	{
	  --i;
	  w0 = a[i];
	  w1 = a[n2 + i];
	}
      while (w0 == w1 && i != 0);
      if (w0 < w1)
	{
	  x = a + n2;
	  y = a;
	}
      else
	{
	  x = a;
	  y = a + n2;
	}
      mpn_sub_n (p, x, y, n2);

      /* Pointwise products. */
      if (BELOW_THRESHOLD (n2, SQR_BASECASE_THRESHOLD))
	{
	  mpn_mul_basecase (ws,    p,      n2, p,      n2);
	  mpn_mul_basecase (p,     a,      n2, a,      n2);
	  mpn_mul_basecase (p + n, a + n2, n2, a + n2, n2);
	}
      else if (BELOW_THRESHOLD (n2, SQR_KARATSUBA_THRESHOLD))
	{
	  mpn_sqr_basecase (ws,    p,      n2);
	  mpn_sqr_basecase (p,     a,      n2);
	  mpn_sqr_basecase (p + n, a + n2, n2);
	}
      else
	{
	  mpn_kara_sqr_n (ws,    p,      n2, ws + n);
	  mpn_kara_sqr_n (p,     a,      n2, ws + n);
	  mpn_kara_sqr_n (p + n, a + n2, n2, ws + n);
	}

      /* Interpolate. */
      w = -mpn_sub_n (ws, p, ws, n);
      w += mpn_add_n (ws, p + n, ws, n);
      w += mpn_add_n (p + n2, p + n2, ws, n);
      MPN_INCR_U (p + n2 + n, 2 * n - (n2 + n), w);
    }
}

/******************************************************************************
 *                                                                            *
 *              Toom 3-way multiplication and squaring                        *
 *                                                                            *
 *****************************************************************************/

/* Starts from:
   {v0,2k}    (stored in {c,2k})
   {vm1,2k+1} (which sign is sa, and absolute value is stored in {vm1,2k+1})
   {v1,2k+1}  (stored in {c+2k,2k+1})
   {v2,2k+1}
   {vinf,twor}  (stored in {c+4k,twor}, except the first limb, saved in vinf0)

   ws is temporary space, and should have at least twor limbs.

   put in {c, 2n} where n = 2k+twor the value of {v0,2k} (already in place)
   + B^k * {tm1, 2k+1}
   + B^(2k) * {t1, 2k+1}
   + B^(3k) * {t2, 2k+1}
   + B^(4k) * {vinf,twor} (high twor-1 limbs already in place)
   where {t1, 2k+1} = ({v1, 2k+1} + sa * {vm1, 2k+1}- 2*{v0,2k})/2-*{vinf,twor}
	 {t2, 2k+1} = (3*({v1,2k+1}-{v0,2k})-sa*{vm1,2k+1}+{v2,2k+1})/6-2*{vinf,twor}
	 {tm1,2k+1} = ({v1,2k+1}-sa*{vm1,2k+1}/2-{t2,2k+1}

   Exact sequence described in a comment in mpn_toom3_mul_n.
   mpn_toom3_mul_n() and mpn_toom3_sqr_n() implement steps 1-2.
   mpn_toom_interpolate_5pts() implements steps 3-4.

   Reference: What About Toom-Cook Matrices Optimality? Marco Bodrato
   and Alberto Zanoni, October 19, 2006, http://bodrato.it/papers/#CIVV2006

   ************* saved note ****************
   Think about:

   The evaluated point a-b+c stands a good chance of having a zero carry
   limb, a+b+c would have a 1/4 chance, and 4*a+2*b+c a 1/8 chance, roughly.
   Perhaps this could be tested and stripped.  Doing so before recursing
   would be better than stripping at the start of mpn_toom3_mul_n/sqr_n,
   since then the recursion could be based on the new size.  Although in
   truth the kara vs toom3 crossover is never so exact that one limb either
   way makes a difference.

   A small value like 1 or 2 for the carry could perhaps also be handled
   with an add_n or addlsh1_n.  Would that be faster than an extra limb on a
   (recursed) multiply/square?
*/

#define TOOM3_MUL_REC(p, a, b, n, ws) \
  do {								\
    if (MUL_TOOM3_THRESHOLD / 3 < MUL_KARATSUBA_THRESHOLD	\
	&& BELOW_THRESHOLD (n, MUL_KARATSUBA_THRESHOLD))	\
      mpn_mul_basecase (p, a, n, b, n);				\
    else if (BELOW_THRESHOLD (n, MUL_TOOM3_THRESHOLD))		\
      mpn_kara_mul_n (p, a, b, n, ws);				\
    else							\
      mpn_toom3_mul_n (p, a, b, n, ws);				\
  } while (0)

#define TOOM3_SQR_REC(p, a, n, ws)				\
  do {								\
    if (SQR_TOOM3_THRESHOLD / 3 < SQR_BASECASE_THRESHOLD	\
	&& BELOW_THRESHOLD (n, SQR_BASECASE_THRESHOLD))		\
      mpn_mul_basecase (p, a, n, a, n);				\
    else if (SQR_TOOM3_THRESHOLD / 3 < SQR_KARATSUBA_THRESHOLD	\
	&& BELOW_THRESHOLD (n, SQR_KARATSUBA_THRESHOLD))	\
      mpn_sqr_basecase (p, a, n);				\
    else if (BELOW_THRESHOLD (n, SQR_TOOM3_THRESHOLD))		\
      mpn_kara_sqr_n (p, a, n, ws);				\
    else							\
      mpn_toom3_sqr_n (p, a, n, ws);				\
  } while (0)

/* The necessary temporary space T(n) satisfies T(n)=0 for n < THRESHOLD,
   and T(n) <= max(2n+2, 6k+3, 4k+3+T(k+1)) otherwise, where k = ceil(n/3).

   Assuming T(n) >= 2n, 6k+3 <= 4k+3+T(k+1).
   Similarly, 2n+2 <= 6k+2 <= 4k+3+T(k+1).

   With T(n) = 2n+S(n), this simplifies to S(n) <= 9 + S(k+1).
   Since THRESHOLD >= 17, we have n/(k+1) >= 19/8
   thus S(n) <= S(n/(19/8)) + 9 thus S(n) <= 9*log(n)/log(19/8) <= 8*log2(n).
*/

void
mpn_toom3_mul_n (mp_ptr c, mp_srcptr a, mp_srcptr b, mp_size_t n, mp_ptr t)
{
  mp_size_t k, k1, kk1, r, twok, twor;
  mp_limb_t cy, cc, saved, vinf0;
  mp_ptr trec;
  int sa, sb;
  mp_ptr c1, c2, c3, c4, c5;

  ASSERT(GMP_NUMB_BITS >= 6);
  ASSERT(n >= 17); /* so that r <> 0 and 5k+3 <= 2n */

  /*
  The algorithm is the following:

  0. k = ceil(n/3), r = n - 2k, B = 2^(GMP_NUMB_BITS), t = B^k
  1. split a and b in three parts each a0, a1, a2 and b0, b1, b2
     with a0, a1, b0, b1 of k limbs, and a2, b2 of r limbs
  2. Evaluation: vm1 may be negative, the other can not.
     v0   <- a0*b0
     v1   <- (a0+a1+a2)*(b0+b1+b2)
     v2   <- (a0+2*a1+4*a2)*(b0+2*b1+4*b2)
     vm1  <- (a0-a1+a2)*(b0-b1+b2)
     vinf <- a2*b2
  3. Interpolation: every result is positive, all divisions are exact
     t2   <- (v2 - vm1)/3
     tm1  <- (v1 - vm1)/2
     t1   <- (v1 - v0)
     t2   <- (t2 - t1)/2
     t1   <- (t1 - tm1 - vinf)
     t2   <- (t2 - 2*vinf)
     tm1  <- (tm1 - t2)
  4. result is c0+c1*t+c2*t^2+c3*t^3+c4*t^4 where
     c0   <- v0
     c1   <- tm1
     c2   <- t1
     c3   <- t2
     c4   <- vinf
  */

  k = (n + 2) / 3; /* ceil(n/3) */
  twok = 2 * k;
  k1 = k + 1;
  kk1 = k + k1;
  r = n - twok;   /* last chunk */
  twor = 2 * r;

  c1 = c + k;
  c2 = c1 + k;
  c3 = c2 + k;
  c4 = c3 + k;
  c5 = c4 + k;

  trec = t + 4 * k + 3; /* trec = v2 + (2k+2) */

  /* put a0+a2 in {c, k+1}, and b0+b2 in {c+4k+2, k+1};
     put a0+a1+a2 in {t, k+1} and b0+b1+b2 in {t+k+1,k+1}
     [????requires 5k+3 <= 2n, ie. n >= 9] */
  cy = mpn_add_n (c,      a, a + twok, r);
  cc = mpn_add_n (c4 + 2, b, b + twok, r);
  if (r < k)
    {
      __GMPN_ADD_1 (cy, c + r,      a + r, k - r, cy);
      __GMPN_ADD_1 (cc, c4 + 2 + r, b + r, k - r, cc);
    }

  /* Put in {t, k+1} the sum
   * (a_0+a_2) - stored in {c, k+1} -
   * +
   * a_1       - stored in {a+k, k} */
  t[k] = (c1[0] = cy) + mpn_add_n (t, c, a + k, k);
  /*          ^              ^
   * carry of a_0 + a_2    carry of (a_0+a_2) + a_1

   */

  /* Put in {t+k+1, k+1} the sum of the two values
   * (b_0+b_2) - stored in {c1+1, k+1} -
   * +
   * b_1       - stored in {b+k, k} */
  t[kk1] = (c5[3] = cc) + mpn_add_n (t + k1, c4 + 2, b + k, k);
  /*          ^              ^
   * carry of b_0 + b_2    carry of (b_0+b_2) + b_1 */

#define v2 (t+2*k+1)

  /* compute v1 := (a0+a1+a2)*(b0+b1+b2) in {t, 2k+1};
     since v1 < 9*B^(2k), v1 uses only 2k+1 words if GMP_NUMB_BITS >= 4 */
  TOOM3_MUL_REC (c2, t, t + k1, k1, trec);

  /*   c         c2    c4                 t
     {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
		 v1                                            */

  /* put |a0-a1+a2| in {c, k+1} and |b0-b1+b2| in {c+4k+2,k+1} */
  /* (They're already there, actually)                         */

  /* sa = sign(a0-a1+a2) */
  sa   = (cy != 0) ? 1 : mpn_cmp (c, a + k, k);
  c[k] = (sa >= 0) ? cy - mpn_sub_n (c, c, a + k, k)
		   : mpn_sub_n (c, a + k, c, k);

  sb    = (cc != 0) ? 1 : mpn_cmp (c4 + 2, b + k, k);
  c5[2] = (sb >= 0) ? cc - mpn_sub_n (c4 + 2, c4 + 2, b + k, k)
		    : mpn_sub_n (c4 + 2, b + k, c4 + 2, k);
  sa *= sb; /* sign of vm1 */

  /* compute vm1 := (a0-a1+a2)*(b0-b1+b2) in {t, 2k+1};
     since |vm1| < 4*B^(2k), vm1 uses only 2k+1 limbs */
  TOOM3_MUL_REC (t, c, c4 + 2, k1, trec);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
		v1                      vm1
  */

  /* compute a0+2a1+4a2 in {c, k+1} and b0+2b1+4b2 in {c+4k+2, k+1}
     [requires 5k+3 <= 2n, i.e. n >= 17] */
#ifdef HAVE_NATIVE_mpn_addlsh1_n
  c1[0] = mpn_addlsh1_n (c, a + k, a + twok, r);
  c5[2] = mpn_addlsh1_n (c4 + 2, b + k, b + twok, r);
  if (r < k)
    {
      __GMPN_ADD_1 (c1[0], c + r, a + k + r, k - r, c1[0]);
      __GMPN_ADD_1 (c5[2], c4 + 2 + r, b + k + r, k - r, c5[2]);
    }
  c1[0] = 2 * c1[0] + mpn_addlsh1_n (c, a, c, k);
  c5[2] = 2 * c5[2] + mpn_addlsh1_n (c4 + 2, b, c4 + 2, k);
#else
  c[r] = mpn_lshift (c, a + twok, r, 1);
  c4[r + 2] = mpn_lshift (c4 + 2, b + twok, r, 1);
  if (r < k)
    {
      MPN_ZERO(c + r + 1, k - r);
      MPN_ZERO(c4 + r + 3, k - r);
    }
  c1[0] += mpn_add_n (c, c, a + k, k);
  c5[2] += mpn_add_n (c4 + 2, c4 + 2, b + k, k);
  mpn_lshift (c, c, k1, 1);
  mpn_lshift (c4 + 2, c4 + 2, k1, 1);
  c1[0] += mpn_add_n (c, c, a, k);
  c5[2] += mpn_add_n (c4 + 2, c4 + 2, b, k);
#endif

  /* compute v2 := (a0+2a1+4a2)*(b0+2b1+4b2) in {t+2k+1, 2k+1}
     v2 < 49*B^k so v2 uses at most 2k+1 limbs if GMP_NUMB_BITS >= 6 */
  TOOM3_MUL_REC (v2, c, c4 + 2, k1, trec);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
		v1                      vm1         v2
  */

  /* compute v0 := a0*b0 in {c, 2k} */
  TOOM3_MUL_REC (c, a, b, k, trec);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       v1                      vm1       v2                   */

  /* compute vinf := a2*b2 in {t+4k+2, 2r}: in {c4, 2r} */

  saved = c4[0];              /* Remember v1's highest byte (will be overwritten). */
  TOOM3_MUL_REC (c4, a + twok, b + twok, r, trec);           /* Overwrites c4[0].  */
  vinf0 = c4[0];              /* Remember vinf's lowest byte (will be overwritten).*/
  c4[0] = saved;              /* Overwriting. Now v1 value is correct.             */

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       v1       vinf[1..]      vm1       v2               */

  mpn_toom_interpolate_5pts (c, v2, t, k, 2*r, sa, vinf0, trec);

#undef v2
}

void
mpn_toom3_sqr_n (mp_ptr c, mp_srcptr a, mp_size_t n, mp_ptr t)
{
  mp_size_t k, k1, kk1, r, twok, twor;
  mp_limb_t cy, saved, vinf0;
  mp_ptr trec;
  int sa;
  mp_ptr c1, c2, c3, c4;

  ASSERT(GMP_NUMB_BITS >= 6);
  ASSERT(n >= 17); /* so that r <> 0 and 5k+3 <= 2n */

  /* the algorithm is the same as mpn_toom3_mul_n, with b=a */

  k = (n + 2) / 3; /* ceil(n/3) */
  twok = 2 * k;
  k1 = k + 1;
  kk1 = k + k1;
  r = n - twok;   /* last chunk */
  twor = 2 * r;

  c1 = c + k;
  c2 = c1 + k;
  c3 = c2 + k;
  c4 = c3 + k;

  trec = t + 4 * k + 3; /* trec = v2 + (2k+2) */

  cy = mpn_add_n (c, a, a + twok, r);
  if (r < k)
    __GMPN_ADD_1 (cy, c + r, a + r, k - r, cy);
  t[k] = (c1[0] = cy) + mpn_add_n (t, c, a + k, k);

#define v2 (t+2*k+1)

  TOOM3_SQR_REC (c2, t, k1, trec);

  sa = (cy != 0) ? 1 : mpn_cmp (c, a + k, k);
  c[k] = (sa >= 0) ? cy - mpn_sub_n (c, c, a + k, k)
    : mpn_sub_n (c, a + k, c, k);

  TOOM3_SQR_REC (t, c, k1, trec);

#ifdef HAVE_NATIVE_mpn_addlsh1_n
  c1[0] = mpn_addlsh1_n (c, a + k, a + twok, r);
  if (r < k)
    __GMPN_ADD_1 (c1[0], c + r, a + k + r, k - r, c1[0]);
  c1[0] = 2 * c1[0] + mpn_addlsh1_n (c, a, c, k);
#else
  c[r] = mpn_lshift (c, a + twok, r, 1);
  if (r < k)
    MPN_ZERO(c + r + 1, k - r);
  c1[0] += mpn_add_n (c, c, a + k, k);
  mpn_lshift (c, c, k1, 1);
  c1[0] += mpn_add_n (c, c, a, k);
#endif

  TOOM3_SQR_REC (v2, c, k1, trec);

  TOOM3_SQR_REC (c, a, k, trec);

  saved = c4[0];
  TOOM3_SQR_REC (c4, a + twok, r, trec);
  vinf0 = c4[0];
  c4[0] = saved;

  mpn_toom_interpolate_5pts (c, v2, t, k, 2*r,  1, vinf0, trec);

#undef v2
}

void
mpn_mul_n (mp_ptr p, mp_srcptr a, mp_srcptr b, mp_size_t n)
{
  ASSERT (n >= 1);
  ASSERT (! MPN_OVERLAP_P (p, 2 * n, a, n));
  ASSERT (! MPN_OVERLAP_P (p, 2 * n, b, n));

  if (BELOW_THRESHOLD (n, MUL_KARATSUBA_THRESHOLD))
    {
      mpn_mul_basecase (p, a, n, b, n);
    }
  else if (BELOW_THRESHOLD (n, MUL_TOOM3_THRESHOLD))
    {
      /* Allocate workspace of fixed size on stack: fast! */
      mp_limb_t ws[MPN_KARA_MUL_N_TSIZE (MUL_TOOM3_THRESHOLD_LIMIT-1)];
      ASSERT (MUL_TOOM3_THRESHOLD <= MUL_TOOM3_THRESHOLD_LIMIT);
      mpn_kara_mul_n (p, a, b, n, ws);
    }
  else if (BELOW_THRESHOLD (n, MUL_TOOM44_THRESHOLD))
    {
      mp_ptr ws;
      TMP_SDECL;
      TMP_SMARK;
      ws = TMP_SALLOC_LIMBS (MPN_TOOM3_MUL_N_TSIZE (n));
      mpn_toom3_mul_n (p, a, b, n, ws);
      TMP_SFREE;
    }
#if WANT_FFT || TUNE_PROGRAM_BUILD
  else if (BELOW_THRESHOLD (n, MUL_FFT_THRESHOLD))
#else
  else if (BELOW_THRESHOLD (n, MPN_TOOM44_MAX_N))
#endif
    {
      mp_ptr ws;
      TMP_SDECL;
      TMP_SMARK;
      ws = TMP_SALLOC_LIMBS (mpn_toom44_mul_itch (n, n));
      mpn_toom44_mul (p, a, n, b, n, ws);
      TMP_SFREE;
    }
  else
#if WANT_FFT || TUNE_PROGRAM_BUILD
    {
      /* The current FFT code allocates its own space.  That should probably
	 change.  */
      mpn_mul_fft_full (p, a, n, b, n);
    }
#else
    {
      /* Toom4 for large operands.  */
      mp_ptr ws;
      TMP_DECL;
      TMP_MARK;
      ws = TMP_BALLOC_LIMBS (mpn_toom44_mul_itch (n, n));
      mpn_toom44_mul (p, a, n, b, n, ws);
      TMP_FREE;
    }
#endif
}

void
mpn_sqr (mp_ptr p, mp_srcptr a, mp_size_t n)
{
  ASSERT (n >= 1);
  ASSERT (! MPN_OVERLAP_P (p, 2 * n, a, n));

#if 0
  /* FIXME: Can this be removed? */
  if (n == 0)
    return;
#endif

  if (BELOW_THRESHOLD (n, SQR_BASECASE_THRESHOLD))
    { /* mul_basecase is faster than sqr_basecase on small sizes sometimes */
      mpn_mul_basecase (p, a, n, a, n);
    }
  else if (BELOW_THRESHOLD (n, SQR_KARATSUBA_THRESHOLD))
    {
      mpn_sqr_basecase (p, a, n);
    }
  else if (BELOW_THRESHOLD (n, SQR_TOOM3_THRESHOLD))
    {
      /* Allocate workspace of fixed size on stack: fast! */
      mp_limb_t ws[MPN_KARA_SQR_N_TSIZE (SQR_TOOM3_THRESHOLD_LIMIT-1)];
      ASSERT (SQR_TOOM3_THRESHOLD <= SQR_TOOM3_THRESHOLD_LIMIT);
      mpn_kara_sqr_n (p, a, n, ws);
    }
  else if (BELOW_THRESHOLD (n, SQR_TOOM4_THRESHOLD))
    {
      mp_ptr ws;
      TMP_SDECL;
      TMP_SMARK;
      ws = TMP_SALLOC_LIMBS (MPN_TOOM3_SQR_N_TSIZE (n));
      mpn_toom3_sqr_n (p, a, n, ws);
      TMP_SFREE;
    }
#if WANT_FFT || TUNE_PROGRAM_BUILD
  else if (BELOW_THRESHOLD (n, SQR_FFT_THRESHOLD))
#else
  else if (BELOW_THRESHOLD (n, MPN_TOOM44_MAX_N))
#endif
    {
      mp_ptr ws;
      TMP_SDECL;
      TMP_SMARK;
      ws = TMP_SALLOC_LIMBS (mpn_toom4_sqr_itch (n));
      mpn_toom4_sqr (p, a, n, ws);
      TMP_SFREE;
    }
  else
#if WANT_FFT || TUNE_PROGRAM_BUILD
    {
      /* The current FFT code allocates its own space.  That should probably
	 change.  */
      mpn_mul_fft_full (p, a, n, a, n);
    }
#else
    {
      /* Toom4 for large operands.  */
      mp_ptr ws;
      TMP_DECL;
      TMP_MARK;
      ws = TMP_BALLOC_LIMBS (mpn_toom4_sqr_itch (n));
      mpn_toom4_sqr (p, a, n, ws);
      TMP_FREE;
    }
#endif
}
