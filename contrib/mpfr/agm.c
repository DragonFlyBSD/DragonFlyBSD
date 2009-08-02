/* mpfr_agm -- arithmetic-geometric mean of two floating-point numbers

Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

/* agm(x,y) is between x and y, so we don't need to save exponent range */
int
mpfr_agm (mpfr_ptr r, mpfr_srcptr op2, mpfr_srcptr op1, mp_rnd_t rnd_mode)
{
  int compare, inexact;
  mp_size_t s;
  mp_prec_t p, q;
  mp_limb_t *up, *vp, *tmpp;
  mpfr_t u, v, tmp;
  unsigned long n; /* number of iterations */
  unsigned long err = 0;
  MPFR_ZIV_DECL (loop);
  MPFR_TMP_DECL(marker);

  MPFR_LOG_FUNC (("op2[%#R]=%R op1[%#R]=%R rnd=%d", op2,op2,op1,op1,rnd_mode),
                 ("r[%#R]=%R inexact=%d", r, r, inexact));

  /* Deal with special values */
  if (MPFR_ARE_SINGULAR (op1, op2))
    {
      /* If a or b is NaN, the result is NaN */
      if (MPFR_IS_NAN(op1) || MPFR_IS_NAN(op2))
        {
          MPFR_SET_NAN(r);
          MPFR_RET_NAN;
        }
      /* now one of a or b is Inf or 0 */
      /* If a and b is +Inf, the result is +Inf.
         Otherwise if a or b is -Inf or 0, the result is NaN */
      else if (MPFR_IS_INF(op1) || MPFR_IS_INF(op2))
        {
          if (MPFR_IS_STRICTPOS(op1) && MPFR_IS_STRICTPOS(op2))
            {
              MPFR_SET_INF(r);
              MPFR_SET_SAME_SIGN(r, op1);
              MPFR_RET(0); /* exact */
            }
          else
            {
              MPFR_SET_NAN(r);
              MPFR_RET_NAN;
            }
        }
      else /* a and b are neither NaN nor Inf, and one is zero */
        {  /* If a or b is 0, the result is +0 since a sqrt is positive */
          MPFR_ASSERTD (MPFR_IS_ZERO (op1) || MPFR_IS_ZERO (op2));
          MPFR_SET_POS (r);
          MPFR_SET_ZERO (r);
          MPFR_RET (0); /* exact */
        }
    }
  MPFR_CLEAR_FLAGS (r);

  /* If a or b is negative (excluding -Infinity), the result is NaN */
  if (MPFR_UNLIKELY(MPFR_IS_NEG(op1) || MPFR_IS_NEG(op2)))
    {
      MPFR_SET_NAN(r);
      MPFR_RET_NAN;
    }

  /* Precision of the following calculus */
  q = MPFR_PREC(r);
  p = q + MPFR_INT_CEIL_LOG2(q) + 15;
  MPFR_ASSERTD (p >= 7); /* see algorithms.tex */
  s = (p - 1) / BITS_PER_MP_LIMB + 1;

  /* b (op2) and a (op1) are the 2 operands but we want b >= a */
  compare = mpfr_cmp (op1, op2);
  if (MPFR_UNLIKELY( compare == 0 ))
    {
      mpfr_set (r, op1, rnd_mode);
      MPFR_RET (0); /* exact */
    }
  else if (compare > 0)
    {
      mpfr_srcptr t = op1;
      op1 = op2;
      op2 = t;
    }
  /* Now b(=op2) >= a (=op1) */

  MPFR_TMP_MARK(marker);

  /* Main loop */
  MPFR_ZIV_INIT (loop, p);
  for (;;)
    {
      mp_prec_t eq;

      /* Init temporary vars */
      MPFR_TMP_INIT (up, u, p, s);
      MPFR_TMP_INIT (vp, v, p, s);
      MPFR_TMP_INIT (tmpp, tmp, p, s);

      /* Calculus of un and vn */
      mpfr_mul (u, op1, op2, GMP_RNDN); /* Faster since PREC(op) < PREC(u) */
      mpfr_sqrt (u, u, GMP_RNDN);
      mpfr_add (v, op1, op2, GMP_RNDN); /* add with !=prec is still good*/
      mpfr_div_2ui (v, v, 1, GMP_RNDN);
      n = 1;
      while (mpfr_cmp2 (u, v, &eq) != 0 && eq <= p - 2)
        {
          mpfr_add (tmp, u, v, GMP_RNDN);
          mpfr_div_2ui (tmp, tmp, 1, GMP_RNDN);
          /* See proof in algorithms.tex */
          if (4*eq > p)
            {
              mpfr_t w;
              /* tmp = U(k) */
              mpfr_init2 (w, (p + 1) / 2);
              mpfr_sub (w, v, u, GMP_RNDN);         /* e = V(k-1)-U(k-1) */
              mpfr_sqr (w, w, GMP_RNDN);            /* e = e^2 */
              mpfr_div_2ui (w, w, 4, GMP_RNDN);     /* e*= (1/2)^2*1/4  */
              mpfr_div (w, w, tmp, GMP_RNDN);       /* 1/4*e^2/U(k) */
              mpfr_sub (v, tmp, w, GMP_RNDN);
              err = MPFR_GET_EXP (tmp) - MPFR_GET_EXP (v); /* 0 or 1 */
              mpfr_clear (w);
              break;
            }
          mpfr_mul (u, u, v, GMP_RNDN);
          mpfr_sqrt (u, u, GMP_RNDN);
          mpfr_swap (v, tmp);
          n ++;
        }
      /* the error on v is bounded by (18n+51) ulps, or twice if there
         was an exponent loss in the final subtraction */
      err += MPFR_INT_CEIL_LOG2(18 * n + 51); /* 18n+51 should not overflow
                                                 since n is about log(p) */
      /* we should have n+2 <= 2^(p/4) [see algorithms.tex] */
      if (MPFR_LIKELY (MPFR_INT_CEIL_LOG2(n + 2) <= p / 4 &&
                       MPFR_CAN_ROUND (v, p - err, q, rnd_mode)))
        break; /* Stop the loop */

      /* Next iteration */
      MPFR_ZIV_NEXT (loop, p);
      s = (p - 1) / BITS_PER_MP_LIMB + 1;
    }
  MPFR_ZIV_FREE (loop);

  /* Setting of the result */
  inexact = mpfr_set (r, v, rnd_mode);

  /* Let's clean */
  MPFR_TMP_FREE(marker);

  return inexact; /* agm(u,v) can be exact for u, v rational only for u=v.
                     Proof (due to Nicolas Brisebarre): it suffices to consider
                     u=1 and v<1. Then 1/AGM(1,v) = 2F1(1/2,1/2,1;1-v^2),
                     and a theorem due to G.V. Chudnovsky states that for x a
                     non-zero algebraic number with |x|<1, then
                     2F1(1/2,1/2,1;x) and 2F1(-1/2,1/2,1;x) are algebraically
                     independent over Q. */
}
