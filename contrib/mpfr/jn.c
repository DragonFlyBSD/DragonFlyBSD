/* mpfr_j0, mpfr_j1, mpfr_jn -- Bessel functions of 1st kind, integer order.
   http://www.opengroup.org/onlinepubs/009695399/functions/j0.html

Copyright 2007, 2008, 2009 Free Software Foundation, Inc.
Contributed by the Arenaire and Cacao projects, INRIA.

This file is part of the GNU MPFR Library.

The GNU MPFR Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

The GNU MPFR Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the GNU MPFR Library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
MA 02110-1301, USA. */

#define MPFR_NEED_LONGLONG_H
#include "mpfr-impl.h"

/* Relations: j(-n,z) = (-1)^n j(n,z)
              j(n,-z) = (-1)^n j(n,z)
*/

static int mpfr_jn_asympt (mpfr_ptr, long, mpfr_srcptr, mp_rnd_t);

int
mpfr_j0 (mpfr_ptr res, mpfr_srcptr z, mp_rnd_t r)
{
  return mpfr_jn (res, 0, z, r);
}

int
mpfr_j1 (mpfr_ptr res, mpfr_srcptr z, mp_rnd_t r)
{
  return mpfr_jn (res, 1, z, r);
}

/* Estimate k0 such that z^2/4 = k0 * (k0 + n)
   i.e., (sqrt(n^2+z^2)-n)/2 = n/2 * (sqrt(1+(z/n)^2) - 1).
   Return min(2*k0/log(2), ULONG_MAX).
*/
static unsigned long
mpfr_jn_k0 (long n, mpfr_srcptr z)
{
  mpfr_t t, u;
  unsigned long k0;

  mpfr_init2 (t, 32);
  mpfr_init2 (u, 32);
  mpfr_div_si (t, z, n, GMP_RNDN);
  mpfr_sqr (t, t, GMP_RNDN);
  mpfr_add_ui (t, t, 1, GMP_RNDN);
  mpfr_sqrt (t, t, GMP_RNDN);
  mpfr_sub_ui (t, t, 1, GMP_RNDN);
  mpfr_mul_si (t, t, n, GMP_RNDN);
  /* the following is a 32-bit approximation to nearest of log(2) */
  mpfr_set_str_binary (u, "0.10110001011100100001011111111");
  mpfr_div (t, t, u, GMP_RNDN);
  if (mpfr_fits_ulong_p (t, GMP_RNDN))
    k0 = mpfr_get_ui (t, GMP_RNDN);
  else
    k0 = ULONG_MAX;
  mpfr_clear (t);
  mpfr_clear (u);
  return k0;
}

int
mpfr_jn (mpfr_ptr res, long n, mpfr_srcptr z, mp_rnd_t r)
{
  int inex;
  unsigned long absn;
  mp_prec_t prec, pbound, err;
  mp_exp_t exps, expT;
  mpfr_t y, s, t, absz;
  unsigned long k, zz, k0;
  MPFR_ZIV_DECL (loop);

  MPFR_LOG_FUNC (("x[%#R]=%R n=%d rnd=%d", z, z, n, r),
                 ("y[%#R]=%R", res, res));

  absn = SAFE_ABS (unsigned long, n);

  if (MPFR_UNLIKELY (MPFR_IS_SINGULAR (z)))
    {
      if (MPFR_IS_NAN (z))
        {
          MPFR_SET_NAN (res);
          MPFR_RET_NAN;
        }
      /* j(n,z) tends to zero when z goes to +Inf or -Inf, oscillating around
         0. We choose to return +0 in that case. */
      else if (MPFR_IS_INF (z)) /* FIXME: according to j(-n,z) = (-1)^n j(n,z)
                                   we might want to give a sign depending on
                                   z and n */
        return mpfr_set_ui (res, 0, r);
      else /* z=0: j(0,0)=1, j(n odd,+/-0) = +/-0 if n > 0, -/+0 if n < 0,
              j(n even,+/-0) = +0 */
        {
          if (n == 0)
            return mpfr_set_ui (res, 1, r);
          else if (absn & 1) /* n odd */
            return (n > 0) ? mpfr_set (res, z, r) : mpfr_neg (res, z, r);
          else /* n even */
            return mpfr_set_ui (res, 0, r);
        }
    }

  /* check for tiny input for j0: j0(z) = 1 - z^2/4 + ..., more precisely
     |j0(z) - 1| <= z^2/4 for -1 <= z <= 1. */
  if (n == 0)
    MPFR_FAST_COMPUTE_IF_SMALL_INPUT (res, __gmpfr_one, -2 * MPFR_GET_EXP (z),
                                      2, 0, r, return _inexact);

  /* idem for j1: j1(z) = z/2 - z^3/16 + ..., more precisely
     |j1(z) - z/2| <= |z^3|/16 for -1 <= z <= 1, with the sign of j1(z) - z/2
     being the opposite of that of z. */
  if (n == 1)
    /* we first compute 2j1(z) = z - z^3/8 + ..., then divide by 2 using
       the "extra" argument of MPFR_FAST_COMPUTE_IF_SMALL_INPUT. */
    MPFR_FAST_COMPUTE_IF_SMALL_INPUT (res, z, -2 * MPFR_GET_EXP (z), 3,
                                      0, r, mpfr_div_2ui (res, res, 1, r));

  /* we can use the asymptotic expansion as soon as |z| > p log(2)/2,
     but to get some margin we use it for |z| > p/2 */
  pbound = MPFR_PREC (res) / 2 + 3;
  MPFR_ASSERTN (pbound <= ULONG_MAX);
  MPFR_ALIAS (absz, z, 1, MPFR_EXP (z));
  if (mpfr_cmp_ui (absz, pbound) > 0)
    {
      inex = mpfr_jn_asympt (res, n, z, r);
      if (inex != 0)
        return inex;
    }

  mpfr_init2 (y, 32);

  /* check underflow case: |j(n,z)| <= 1/sqrt(2 Pi n) (ze/2n)^n
     (see algorithms.tex) */
  if (absn > 0)
    {
      /* the following is an upper 32-bit approximation of exp(1)/2 */
      mpfr_set_str_binary (y, "1.0101101111110000101010001011001");
      if (MPFR_SIGN(z) > 0)
        mpfr_mul (y, y, z, GMP_RNDU);
      else
        {
          mpfr_mul (y, y, z, GMP_RNDD);
          mpfr_neg (y, y, GMP_RNDU);
        }
      mpfr_div_ui (y, y, absn, GMP_RNDU);
      /* now y is an upper approximation of |ze/2n|: y < 2^EXP(y),
         thus |j(n,z)| < 1/2*y^n < 2^(n*EXP(y)-1).
         If n*EXP(y) < __gmpfr_emin then we have an underflow.
         Warning: absn is an unsigned long. */
      if ((MPFR_EXP(y) < 0 && absn > (unsigned long) (-__gmpfr_emin))
          || (absn <= (unsigned long) (-MPFR_EMIN_MIN) &&
              MPFR_EXP(y) < __gmpfr_emin / (mp_exp_t) absn))
        {
          mpfr_clear (y);
          return mpfr_underflow (res, (r == GMP_RNDN) ? GMP_RNDZ : r,
                         (n % 2) ? ((n > 0) ? MPFR_SIGN(z) : -MPFR_SIGN(z))
                                 : MPFR_SIGN_POS);
        }
    }

  mpfr_init (s);
  mpfr_init (t);

  /* the logarithm of the ratio between the largest term in the series
     and the first one is roughly bounded by k0, which we add to the
     working precision to take into account this cancellation */
  k0 = mpfr_jn_k0 (absn, z);
  prec = MPFR_PREC (res) + k0 + 2 * MPFR_INT_CEIL_LOG2 (MPFR_PREC (res)) + 3;

  MPFR_ZIV_INIT (loop, prec);
  for (;;)
    {
      mpfr_set_prec (y, prec);
      mpfr_set_prec (s, prec);
      mpfr_set_prec (t, prec);
      mpfr_pow_ui (t, z, absn, GMP_RNDN); /* z^|n| */
      mpfr_mul (y, z, z, GMP_RNDN);       /* z^2 */
      zz = mpfr_get_ui (y, GMP_RNDU);
      MPFR_ASSERTN (zz < ULONG_MAX);
      mpfr_div_2ui (y, y, 2, GMP_RNDN);   /* z^2/4 */
      mpfr_fac_ui (s, absn, GMP_RNDN);    /* |n|! */
      mpfr_div (t, t, s, GMP_RNDN);
      if (absn > 0)
        mpfr_div_2ui (t, t, absn, GMP_RNDN);
      mpfr_set (s, t, GMP_RNDN);
      exps = MPFR_EXP (s);
      expT = exps;
      for (k = 1; ; k++)
        {
          mpfr_mul (t, t, y, GMP_RNDN);
          mpfr_neg (t, t, GMP_RNDN);
          if (k + absn <= ULONG_MAX / k)
            mpfr_div_ui (t, t, k * (k + absn), GMP_RNDN);
          else
            {
              mpfr_div_ui (t, t, k, GMP_RNDN);
              mpfr_div_ui (t, t, k + absn, GMP_RNDN);
            }
          exps = MPFR_EXP (t);
          if (exps > expT)
            expT = exps;
          mpfr_add (s, s, t, GMP_RNDN);
          exps = MPFR_EXP (s);
          if (exps > expT)
            expT = exps;
          if (MPFR_EXP (t) + (mp_exp_t) prec <= MPFR_EXP (s) &&
              zz / (2 * k) < k + n)
            break;
        }
      /* the error is bounded by (4k^2+21/2k+7) ulp(s)*2^(expT-exps)
         <= (k+2)^2 ulp(s)*2^(2+expT-exps) */
      err = 2 * MPFR_INT_CEIL_LOG2(k + 2) + 2 + expT - MPFR_EXP (s);
      if (MPFR_LIKELY (MPFR_CAN_ROUND (s, prec - err, MPFR_PREC(res), r)))
          break;
      MPFR_ZIV_NEXT (loop, prec);
    }
  MPFR_ZIV_FREE (loop);

  inex = ((n >= 0) || ((n & 1) == 0)) ? mpfr_set (res, s, r)
                                      : mpfr_neg (res, s, r);

  mpfr_clear (y);
  mpfr_clear (s);
  mpfr_clear (t);

  return inex;
}

#define MPFR_JN
#include "jyn_asympt.c"
