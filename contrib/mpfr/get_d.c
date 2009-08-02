/* mpfr_get_d, mpfr_get_d_2exp -- convert a multiple precision floating-point
                                  number to a machine double precision float

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

#include <float.h>

#define MPFR_NEED_LONGLONG_H
#include "mpfr-impl.h"

/* "double" NaN and infinities are written as explicit bytes to be sure of
   getting what we want, and to be sure of not depending on libm.

   Could use 4-byte "float" values and let the code convert them, but it
   seems more direct to give exactly what we want.  Certainly for gcc 3.0.2
   on alphaev56-unknown-freebsd4.3 the NaN must be 8-bytes, since that
   compiler+system was seen incorrectly converting from a "float" NaN.  */

#if _GMP_IEEE_FLOATS

/* The "d" field guarantees alignment to a suitable boundary for a double.
   Could use a union instead, if we checked the compiler supports union
   initializers.  */
struct dbl_bytes {
  unsigned char b[8];
  double d;
};

#define MPFR_DBL_INFP  (* (const double *) dbl_infp.b)
#define MPFR_DBL_INFM  (* (const double *) dbl_infm.b)
#define MPFR_DBL_NAN   (* (const double *) dbl_nan.b)

#if HAVE_DOUBLE_IEEE_LITTLE_ENDIAN
static const struct dbl_bytes dbl_infp =
  { { 0, 0, 0, 0, 0, 0, 0xF0, 0x7F }, 0.0 };
static const struct dbl_bytes dbl_infm =
  { { 0, 0, 0, 0, 0, 0, 0xF0, 0xFF }, 0.0 };
static const struct dbl_bytes dbl_nan  =
  { { 0, 0, 0, 0, 0, 0, 0xF8, 0x7F }, 0.0 };
#endif
#if HAVE_DOUBLE_IEEE_LITTLE_SWAPPED
static const struct dbl_bytes dbl_infp =
  { { 0, 0, 0xF0, 0x7F, 0, 0, 0, 0 }, 0.0 };
static const struct dbl_bytes dbl_infm =
  { { 0, 0, 0xF0, 0xFF, 0, 0, 0, 0 }, 0.0 };
static const struct dbl_bytes dbl_nan  =
  { { 0, 0, 0xF8, 0x7F, 0, 0, 0, 0 }, 0.0 };
#endif
#if HAVE_DOUBLE_IEEE_BIG_ENDIAN
static const struct dbl_bytes dbl_infp =
  { { 0x7F, 0xF0, 0, 0, 0, 0, 0, 0 }, 0.0 };
static const struct dbl_bytes dbl_infm =
  { { 0xFF, 0xF0, 0, 0, 0, 0, 0, 0 }, 0.0 };
static const struct dbl_bytes dbl_nan  =
  { { 0x7F, 0xF8, 0, 0, 0, 0, 0, 0 }, 0.0 };
#endif

#else /* _GMP_IEEE_FLOATS */

#define MPFR_DBL_INFP DBL_POS_INF
#define MPFR_DBL_INFM DBL_NEG_INF
#define MPFR_DBL_NAN DBL_NAN

#endif /* _GMP_IEEE_FLOATS */


/* multiplies 1/2 <= d <= 1 by 2^exp */
static double
mpfr_scale2 (double d, int exp)
{
#if _GMP_IEEE_FLOATS
  {
    union ieee_double_extract x;

    if (MPFR_UNLIKELY (d == 1.0))
      {
        d = 0.5;
        exp ++;
      }

    /* now 1/2 <= d < 1 */

    /* infinities and zeroes have already been checked */
    MPFR_ASSERTD (-1073 <= exp && exp <= 1025);

    x.d = d;
    if (MPFR_UNLIKELY (exp < -1021)) /* subnormal case */
      {
        x.s.exp += exp + 52;
        x.d *= DBL_EPSILON;
      }
    else /* normalized case */
      {
        x.s.exp += exp;
      }
    return x.d;
  }
#else /* _GMP_IEEE_FLOATS */
  {
    double factor;

    /* An overflow may occurs (example: 0.5*2^1024) */
    if (d < 1.0)
      {
        d += d;
        exp--;
      }
    /* Now 1.0 <= d < 2.0 */

    if (exp < 0)
      {
        factor = 0.5;
        exp = -exp;
      }
    else
      {
        factor = 2.0;
      }
    while (exp != 0)
      {
        if ((exp & 1) != 0)
          d *= factor;
        exp >>= 1;
        factor *= factor;
      }
    return d;
  }
#endif
}

/* Assumes IEEE-754 double precision; otherwise, only an approximated
   result will be returned, without any guaranty (and special cases
   such as NaN must be avoided if not supported). */

double
mpfr_get_d (mpfr_srcptr src, mp_rnd_t rnd_mode)
{
  double d;
  int negative;
  mp_exp_t e;

  if (MPFR_UNLIKELY (MPFR_IS_SINGULAR (src)))
    {
      if (MPFR_IS_NAN (src))
        return MPFR_DBL_NAN;

      negative = MPFR_IS_NEG (src);

      if (MPFR_IS_INF (src))
        return negative ? MPFR_DBL_INFM : MPFR_DBL_INFP;

      MPFR_ASSERTD (MPFR_IS_ZERO(src));
      return negative ? DBL_NEG_ZERO : 0.0;
    }

  e = MPFR_GET_EXP (src);
  negative = MPFR_IS_NEG (src);

  /* the smallest normalized number is 2^(-1022)=0.1e-1021, and the smallest
     subnormal is 2^(-1074)=0.1e-1073 */
  if (MPFR_UNLIKELY (e < -1073))
    {
      /* Note: Avoid using a constant expression DBL_MIN * DBL_EPSILON
         as this gives 0 instead of the correct result with gcc on some
         Alpha machines. */
      d = negative ?
        (rnd_mode == GMP_RNDD ||
         (rnd_mode == GMP_RNDN && mpfr_cmp_si_2exp(src, -1, -1075) < 0)
         ? -DBL_MIN : DBL_NEG_ZERO) :
        (rnd_mode == GMP_RNDU ||
         (rnd_mode == GMP_RNDN && mpfr_cmp_si_2exp(src, 1, -1075) > 0)
         ? DBL_MIN : 0.0);
      if (d != 0.0)
        d *= DBL_EPSILON;
    }
  /* the largest normalized number is 2^1024*(1-2^(-53))=0.111...111e1024 */
  else if (MPFR_UNLIKELY (e > 1024))
    {
      d = negative ?
        (rnd_mode == GMP_RNDZ || rnd_mode == GMP_RNDU ?
         -DBL_MAX : MPFR_DBL_INFM) :
        (rnd_mode == GMP_RNDZ || rnd_mode == GMP_RNDD ?
         DBL_MAX : MPFR_DBL_INFP);
    }
  else
    {
      int nbits;
      mp_size_t np, i;
      mp_limb_t tp[ MPFR_LIMBS_PER_DOUBLE ];
      int carry;

      nbits = IEEE_DBL_MANT_DIG; /* 53 */
      if (MPFR_UNLIKELY (e < -1021))
        /*In the subnormal case, compute the exact number of significant bits*/
        {
          nbits += (1021 + e);
          MPFR_ASSERTD (nbits >= 1);
        }
      np = (nbits + BITS_PER_MP_LIMB - 1) / BITS_PER_MP_LIMB;
      MPFR_ASSERTD ( np <= MPFR_LIMBS_PER_DOUBLE );
      carry = mpfr_round_raw_4 (tp, MPFR_MANT(src), MPFR_PREC(src), negative,
                                nbits, rnd_mode);
      if (MPFR_UNLIKELY(carry))
        d = 1.0;
      else
        {
          /* The following computations are exact thanks to the previous
             mpfr_round_raw. */
          d = (double) tp[0] / MP_BASE_AS_DOUBLE;
          for (i = 1 ; i < np ; i++)
            d = (d + tp[i]) / MP_BASE_AS_DOUBLE;
          /* d is the mantissa (between 1/2 and 1) of the argument rounded
             to 53 bits */
        }
      d = mpfr_scale2 (d, e);
      if (negative)
        d = -d;
    }

  return d;
}

#undef mpfr_get_d1
double
mpfr_get_d1 (mpfr_srcptr src)
{
  return mpfr_get_d (src, __gmpfr_default_rounding_mode);
}

double
mpfr_get_d_2exp (long *expptr, mpfr_srcptr src, mp_rnd_t rnd_mode)
{
  double ret;
  mp_exp_t exp;
  mpfr_t tmp;

  if (MPFR_UNLIKELY (MPFR_IS_SINGULAR (src)))
    {
      int negative;
      *expptr = 0;
      if (MPFR_IS_NAN (src))
        return MPFR_DBL_NAN;
      negative = MPFR_IS_NEG (src);
      if (MPFR_IS_INF (src))
        return negative ? MPFR_DBL_INFM : MPFR_DBL_INFP;
      MPFR_ASSERTD (MPFR_IS_ZERO(src));
      return negative ? DBL_NEG_ZERO : 0.0;
    }

  tmp[0] = *src;        /* Hack copy mpfr_t */
  MPFR_SET_EXP (tmp, 0);
  ret = mpfr_get_d (tmp, rnd_mode);

  if (MPFR_IS_PURE_FP(src))
    {
      exp = MPFR_GET_EXP (src);

      /* rounding can give 1.0, adjust back to 0.5 <= abs(ret) < 1.0 */
      if (ret == 1.0)
        {
          ret = 0.5;
          exp++;
        }
      else if (ret == -1.0)
        {
          ret = -0.5;
          exp++;
        }

      MPFR_ASSERTN ((ret >= 0.5 && ret < 1.0)
                    || (ret <= -0.5 && ret > -1.0));
      MPFR_ASSERTN (exp >= LONG_MIN && exp <= LONG_MAX);
    }
  else
    exp = 0;

  *expptr = exp;
  return ret;
}
