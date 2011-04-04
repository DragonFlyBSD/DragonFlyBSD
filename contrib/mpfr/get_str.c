/* mpfr_get_str -- output a floating-point number to a string

Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
Contributed by the Arenaire and Cacao projects, INRIA.
Contributed by Alain Delplanque and Paul Zimmermann.

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

static int mpfr_get_str_aux (char *const, mp_exp_t *const, mp_limb_t *const,
                       mp_size_t, mp_exp_t, long, int, size_t, mp_rnd_t);

/* The implicit \0 is useless, but we do not write num_to_text[36]
   otherwise g++ complains. */
static const char num_to_text[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/* copy most important limbs of {op, n2} in {rp, n1} */
/* if n1 > n2 put 0 in low limbs of {rp, n1} */
#define MPN_COPY2(rp, n1, op, n2) \
  if ((n1) <= (n2)) \
    { \
      MPN_COPY ((rp), (op) + (n2) - (n1), (n1)); \
    } \
  else \
    { \
      MPN_COPY ((rp) + (n1) - (n2), (op), (n2)); \
      MPN_ZERO ((rp), (n1) - (n2)); \
    }

#define MPFR_ROUND_FAILED 3

/* Input: an approximation r*2^f of a real Y, with |r*2^f-Y| <= 2^(e+f).
   Returns if possible in the string s the mantissa corresponding to
   the integer nearest to Y, within the direction rnd, and returns the
   exponent in exp.
   n is the number of limbs of r.
   e represents the maximal error in the approximation of Y
      (e < 0 iff the approximation is exact, i.e., r*2^f = Y).
   b is the wanted base (2 <= b <= 36).
   m is the number of wanted digits in the mantissa.
   rnd is the rounding mode.
   It is assumed that b^(m-1) <= Y < b^(m+1), thus the returned value
   satisfies b^(m-1) <= rnd(Y) < b^(m+1).

   Rounding may fail for two reasons:
   - the error is too large to determine the integer N nearest to Y
   - either the number of digits of N in base b is too large (m+1),
     N=2*N1+(b/2) and the rounding mode is to nearest. This can
     only happen when b is even.

   Return value:
   - the direction of rounding (-1, 0, 1) if rounding is possible
   - -MPFR_ROUND_FAILED if rounding not possible because m+1 digits
   - MPFR_ROUND_FAILED otherwise (too large error)
*/
static int
mpfr_get_str_aux (char *const str, mp_exp_t *const exp, mp_limb_t *const r,
                  mp_size_t n, mp_exp_t f, long e, int b, size_t m,
                  mp_rnd_t rnd)
{
  int dir;                  /* direction of the rounded result */
  mp_limb_t ret = 0;        /* possible carry in addition */
  mp_size_t i0, j0;         /* number of limbs and bits of Y */
  unsigned char *str1;      /* string of m+2 characters */
  size_t size_s1;           /* length of str1 */
  mp_rnd_t rnd1;
  size_t i;
  int exact = (e < 0);
  MPFR_TMP_DECL(marker);

  /* if f > 0, then the maximal error 2^(e+f) is larger than 2 so we can't
     determine the integer Y */
  MPFR_ASSERTN(f <= 0);
  /* if f is too small, then r*2^f is smaller than 1 */
  MPFR_ASSERTN(f > (-n * BITS_PER_MP_LIMB));

  MPFR_TMP_MARK(marker);

  /* R = 2^f sum r[i]K^(i)
     r[i] = (r_(i,k-1)...r_(i,0))_2
     R = sum r(i,j)2^(j+ki+f)
     the bits from R are referenced by pairs (i,j) */

  /* check if is possible to round r with rnd mode
     where |r*2^f-Y| <= 2^(e+f)
     the exponent of R is: f + n*BITS_PER_MP_LIMB
     we must have e + f == f + n*BITS_PER_MP_LIMB - err
     err = n*BITS_PER_MP_LIMB - e
     R contains exactly -f bits after the integer point:
     to determine the nearest integer, we thus need a precision of
     n * BITS_PER_MP_LIMB + f */

  if (exact || mpfr_can_round_raw (r, n, (mp_size_t) 1,
            n * BITS_PER_MP_LIMB - e, GMP_RNDN, rnd, n * BITS_PER_MP_LIMB + f))
    {
      /* compute the nearest integer to R */

      /* bit of weight 0 in R has position j0 in limb r[i0] */
      i0 = (-f) / BITS_PER_MP_LIMB;
      j0 = (-f) % BITS_PER_MP_LIMB;

      ret = mpfr_round_raw (r + i0, r, n * BITS_PER_MP_LIMB, 0,
                            n * BITS_PER_MP_LIMB + f, rnd, &dir);
      MPFR_ASSERTD(dir != MPFR_ROUND_FAILED);

      /* warning: mpfr_round_raw_generic returns MPFR_EVEN_INEX (2) or
         -MPFR_EVEN_INEX (-2) in case of even rounding */

      if (ret) /* Y is a power of 2 */
        {
          if (j0)
            r[n - 1] = MPFR_LIMB_HIGHBIT >> (j0 - 1);
          else /* j0=0, necessarily i0 >= 1 otherwise f=0 and r is exact */
            {
              r[n - 1] = ret;
              r[--i0] = 0; /* set to zero the new low limb */
            }
        }
      else /* shift r to the right by (-f) bits (i0 already done) */
        {
          if (j0)
            mpn_rshift (r + i0, r + i0, n - i0, j0);
        }

      /* now the rounded value Y is in {r+i0, n-i0} */

      /* convert r+i0 into base b */
      str1 = (unsigned char*) MPFR_TMP_ALLOC (m + 3); /* need one extra character for mpn_get_str */
      size_s1 = mpn_get_str (str1, b, r + i0, n - i0);

      /* round str1 */
      MPFR_ASSERTN(size_s1 >= m);
      *exp = size_s1 - m; /* number of superfluous characters */

      /* if size_s1 = m + 2, necessarily we have b^(m+1) as result,
         and the result will not change */

      /* so we have to double-round only when size_s1 = m + 1 and
         (i) the result is inexact
         (ii) or the last digit is non-zero */
      if ((size_s1 == m + 1) && ((dir != 0) || (str1[size_s1 - 1] != 0)))
        {
          /* rounding mode */
          rnd1 = rnd;

          /* round to nearest case */
          if (rnd == GMP_RNDN)
            {
              if (2 * str1[size_s1 - 1] == b)
                {
                  if (dir == 0 && exact) /* exact: even rounding */
                    {
                      rnd1 = ((str1[size_s1 - 2] & 1) == 0)
                        ? GMP_RNDD : GMP_RNDU;
                    }
                  else
                    {
                      /* otherwise we cannot round correctly: for example
                         if b=10, we might have a mantissa of
                         xxxxxxx5.00000000 which can be rounded to nearest
                         to 8 digits but not to 7 */
                      dir = -MPFR_ROUND_FAILED;
                      MPFR_ASSERTD(dir != MPFR_EVEN_INEX);
                      goto free_and_return;
                    }
                }
              else if (2 * str1[size_s1 - 1] < b)
                rnd1 = GMP_RNDD;
              else
                rnd1 = GMP_RNDU;
            }

          /* now rnd1 is either GMP_RNDD or GMP_RNDZ -> truncate
                             or GMP_RDNU -> round toward infinity */

          /* round away from zero */
          if (rnd1 == GMP_RNDU)
            {
              if (str1[size_s1 - 1] != 0)
                {
                  /* the carry cannot propagate to the whole string, since
                     Y = x*b^(m-g) < 2*b^m <= b^(m+1)-b
                     where x is the input float */
                  MPFR_ASSERTN(size_s1 >= 2);
                  i = size_s1 - 2;
                  while (str1[i] == b - 1)
                    {
                      MPFR_ASSERTD(i > 0);
                      str1[i--] = 0;
                    }
                  str1[i]++;
                }
              dir = 1;
            }
          /* round toward zero (truncate) */
          else
            dir = -1;
        }

      /* copy str1 into str and convert to characters (digits and
         lowercase letters from the source character set) */
      for (i = 0; i < m; i++)
        str[i] = num_to_text[(int) str1[i]]; /* str1[i] is an unsigned char */
      str[m] = 0;
    }
  /* mpfr_can_round_raw failed: rounding is not possible */
  else
    {
      dir = MPFR_ROUND_FAILED; /* should be different from MPFR_EVEN_INEX */
      MPFR_ASSERTD(dir != MPFR_EVEN_INEX);
    }

 free_and_return:
  MPFR_TMP_FREE(marker);

  return dir;
}

/***************************************************************************
 * __gmpfr_l2b[b-2][0] is a 23-bit upper approximation to log(b)/log(2),   *
 * __gmpfr_l2b[b-2][1] is a 76-bit upper approximation to log(2)/log(b).   *
 * The following code is generated by tests/tl2b (with an argument).       *
 ***************************************************************************/

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_2_0__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x00000000, 0x00000000, 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x0000000000000000, 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_2_1__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0x0e00, 0xcae0 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0xcae00e00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0xcae00e0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0xcae00e000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0xcae00e00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_3_0__tab[] = { 0xcae00e0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0x0448, 0xe94e, 0xa9a9, 0x9cc1, 0xa184 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0x04480000, 0xa9a9e94e, 0xa1849cc1 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0x0448000000000000, 0xa1849cc1a9a9e94e };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0xa1849cc1a9a9e94e04480000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0xa1849cc1a9a9e94e0448000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_3_1__tab[] = { 0xa1849cc1a9a9e94e044800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_4_0__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x00000000, 0x00000000, 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x0000000000000000, 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_4_1__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x7a00, 0x949a };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x949a7a00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x949a7a0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x949a7a000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x949a7a00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_5_0__tab[] = { 0x949a7a0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0x67b8, 0x9728, 0x287b, 0xa348, 0xdc81 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0x67b80000, 0x287b9728, 0xdc81a348 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0x67b8000000000000, 0xdc81a348287b9728 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0xdc81a348287b972867b80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0xdc81a348287b972867b8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_5_1__tab[] = { 0xdc81a348287b972867b800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0x0800, 0xa570 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0xa5700800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0xa570080000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0xa57008000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0xa5700800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_6_0__tab[] = { 0xa570080000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xff10, 0xf9e9, 0xe054, 0x9236, 0xc611 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xff100000, 0xe054f9e9, 0xc6119236 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xff10000000000000, 0xc6119236e054f9e9 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xc6119236e054f9e9ff100000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xc6119236e054f9e9ff10000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_6_1__tab[] = { 0xc6119236e054f9e9ff1000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb400, 0xb3ab };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb3abb400 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb3abb40000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb3abb4000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb3abb400000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_7_0__tab[] = { 0xb3abb40000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0x37b8, 0xa711, 0x754d, 0xc9d6, 0xb660 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0x37b80000, 0x754da711, 0xb660c9d6 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0x37b8000000000000, 0xb660c9d6754da711 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0xb660c9d6754da71137b80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0xb660c9d6754da71137b8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_7_1__tab[] = { 0xb660c9d6754da71137b800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0x0000, 0xc000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0xc0000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0xc000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0xc00000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0xc0000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_8_0__tab[] = { 0xc000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaab0, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaab00000, 0xaaaaaaaa, 0xaaaaaaaa };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaab0000000000000, 0xaaaaaaaaaaaaaaaa };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaaaaaaaaaaaaaaaaaab00000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaaaaaaaaaaaaaaaaaab0000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_8_1__tab[] = { 0xaaaaaaaaaaaaaaaaaab000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0x0e00, 0xcae0 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0xcae00e00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0xcae00e0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0xcae00e000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0xcae00e00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_9_0__tab[] = { 0xcae00e0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0x0448, 0xe94e, 0xa9a9, 0x9cc1, 0xa184 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0x04480000, 0xa9a9e94e, 0xa1849cc1 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0x0448000000000000, 0xa1849cc1a9a9e94e };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0xa1849cc1a9a9e94e04480000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0xa1849cc1a9a9e94e0448000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_9_1__tab[] = { 0xa1849cc1a9a9e94e044800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0x7a00, 0xd49a };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0xd49a7a00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0xd49a7a0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0xd49a7a000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0xd49a7a00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_10_0__tab[] = { 0xd49a7a0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x8f90, 0xf798, 0xfbcf, 0x9a84, 0x9a20 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x8f900000, 0xfbcff798, 0x9a209a84 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x8f90000000000000, 0x9a209a84fbcff798 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x9a209a84fbcff7988f900000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x9a209a84fbcff7988f90000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_10_1__tab[] = { 0x9a209a84fbcff7988f9000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0x5400, 0xdd67 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0xdd675400 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0xdd67540000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0xdd6754000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0xdd675400000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_11_0__tab[] = { 0xdd67540000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0xe170, 0x9d10, 0xeb22, 0x4e0e, 0x9400 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0xe1700000, 0xeb229d10, 0x94004e0e };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0xe170000000000000, 0x94004e0eeb229d10 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0x94004e0eeb229d10e1700000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0x94004e0eeb229d10e170000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_11_1__tab[] = { 0x94004e0eeb229d10e17000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0x0800, 0xe570 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0xe5700800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0xe570080000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0xe57008000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0xe5700800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_12_0__tab[] = { 0xe570080000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0xfe28, 0x1c24, 0x0b03, 0x9c1a, 0x8ed1 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0xfe280000, 0x0b031c24, 0x8ed19c1a };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0xfe28000000000000, 0x8ed19c1a0b031c24 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0x8ed19c1a0b031c24fe280000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0x8ed19c1a0b031c24fe28000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_12_1__tab[] = { 0x8ed19c1a0b031c24fe2800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0x0200, 0xecd4 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0xecd40200 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0xecd4020000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0xecd402000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0xecd40200000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_13_0__tab[] = { 0xecd4020000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x57f8, 0xf7b4, 0xcb20, 0xa7c6, 0x8a5c };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x57f80000, 0xcb20f7b4, 0x8a5ca7c6 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x57f8000000000000, 0x8a5ca7c6cb20f7b4 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x8a5ca7c6cb20f7b457f80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x8a5ca7c6cb20f7b457f8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_13_1__tab[] = { 0x8a5ca7c6cb20f7b457f800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xb400, 0xf3ab };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xf3abb400 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xf3abb40000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xf3abb4000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xf3abb400000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_14_0__tab[] = { 0xf3abb40000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x85a8, 0x5cab, 0x96b5, 0xfff6, 0x8679 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x85a80000, 0x96b55cab, 0x8679fff6 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x85a8000000000000, 0x8679fff696b55cab };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x8679fff696b55cab85a80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x8679fff696b55cab85a8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_14_1__tab[] = { 0x8679fff696b55cab85a800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0x8000, 0xfa0a };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0xfa0a8000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0xfa0a800000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0xfa0a80000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0xfa0a8000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_15_0__tab[] = { 0xfa0a800000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x6f80, 0xa6aa, 0x69f0, 0xee23, 0x830c };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x6f800000, 0x69f0a6aa, 0x830cee23 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x6f80000000000000, 0x830cee2369f0a6aa };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x830cee2369f0a6aa6f800000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x830cee2369f0a6aa6f80000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_15_1__tab[] = { 0x830cee2369f0a6aa6f8000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_16_0__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x8000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x00000000, 0x00000000, 0x80000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x0000000000000000, 0x8000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x800000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x80000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_16_1__tab[] = { 0x8000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x8000, 0x82cc };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x82cc8000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x82cc800000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x82cc80000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x82cc8000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_17_0__tab[] = { 0x82cc800000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0x8720, 0x259b, 0x62c4, 0xabf5, 0xfa85 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0x87200000, 0x62c4259b, 0xfa85abf5 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0x8720000000000000, 0xfa85abf562c4259b };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0xfa85abf562c4259b87200000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0xfa85abf562c4259b8720000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_17_1__tab[] = { 0xfa85abf562c4259b872000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x0800, 0x8570 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x85700800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x8570080000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x857008000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x85700800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_18_0__tab[] = { 0x8570080000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0x3698, 0x1378, 0x5537, 0x6634, 0xf591 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0x36980000, 0x55371378, 0xf5916634 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0x3698000000000000, 0xf591663455371378 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0xf59166345537137836980000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0xf5916634553713783698000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_18_1__tab[] = { 0xf591663455371378369800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x0600, 0x87ef };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x87ef0600 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x87ef060000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x87ef06000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x87ef0600000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_19_0__tab[] = { 0x87ef060000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0x0db8, 0x558c, 0x62ed, 0x08c0, 0xf10f };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0x0db80000, 0x62ed558c, 0xf10f08c0 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0x0db8000000000000, 0xf10f08c062ed558c };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0xf10f08c062ed558c0db80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0xf10f08c062ed558c0db8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_19_1__tab[] = { 0xf10f08c062ed558c0db800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x3e00, 0x8a4d };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x8a4d3e00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x8a4d3e0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x8a4d3e000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x8a4d3e00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_20_0__tab[] = { 0x8a4d3e0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0x0b40, 0xa71c, 0x1cc1, 0x690a, 0xecee };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0x0b400000, 0x1cc1a71c, 0xecee690a };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0x0b40000000000000, 0xecee690a1cc1a71c };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0xecee690a1cc1a71c0b400000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0xecee690a1cc1a71c0b40000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_20_1__tab[] = { 0xecee690a1cc1a71c0b4000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0xde00, 0x8c8d };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0x8c8dde00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0x8c8dde0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0x8c8dde000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0x8c8dde00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_21_0__tab[] = { 0x8c8dde0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0x4108, 0x6b26, 0xb3d0, 0x63c1, 0xe922 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0x41080000, 0xb3d06b26, 0xe92263c1 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0x4108000000000000, 0xe92263c1b3d06b26 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0xe92263c1b3d06b2641080000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0xe92263c1b3d06b264108000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_21_1__tab[] = { 0xe92263c1b3d06b26410800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0xaa00, 0x8eb3 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0x8eb3aa00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0x8eb3aa0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0x8eb3aa000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0x8eb3aa00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_22_0__tab[] = { 0x8eb3aa0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xdbe8, 0xf061, 0x60b9, 0x2c4d, 0xe5a0 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xdbe80000, 0x60b9f061, 0xe5a02c4d };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xdbe8000000000000, 0xe5a02c4d60b9f061 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xe5a02c4d60b9f061dbe80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xe5a02c4d60b9f061dbe8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_22_1__tab[] = { 0xe5a02c4d60b9f061dbe800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x0600, 0x90c1 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x90c10600 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x90c1060000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x90c106000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x90c10600000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_23_0__tab[] = { 0x90c1060000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xc3e0, 0x586a, 0x46b9, 0xcadd, 0xe25e };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xc3e00000, 0x46b9586a, 0xe25ecadd };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xc3e0000000000000, 0xe25ecadd46b9586a };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xe25ecadd46b9586ac3e00000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xe25ecadd46b9586ac3e0000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_23_1__tab[] = { 0xe25ecadd46b9586ac3e000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x0400, 0x92b8 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x92b80400 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x92b8040000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x92b804000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x92b80400000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_24_0__tab[] = { 0x92b8040000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0x3668, 0x7263, 0xc7c6, 0xbb44, 0xdf56 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0x36680000, 0xc7c67263, 0xdf56bb44 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0x3668000000000000, 0xdf56bb44c7c67263 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0xdf56bb44c7c6726336680000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0xdf56bb44c7c672633668000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_24_1__tab[] = { 0xdf56bb44c7c67263366800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x7a00, 0x949a };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x949a7a00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x949a7a0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x949a7a000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x949a7a00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_25_0__tab[] = { 0x949a7a0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0x67b8, 0x9728, 0x287b, 0xa348, 0xdc81 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0x67b80000, 0x287b9728, 0xdc81a348 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0x67b8000000000000, 0xdc81a348287b9728 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0xdc81a348287b972867b80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0xdc81a348287b972867b8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_25_1__tab[] = { 0xdc81a348287b972867b800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x0200, 0x966a };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x966a0200 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x966a020000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x966a02000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x966a0200000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_26_0__tab[] = { 0x966a020000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0x6458, 0x78a4, 0x7583, 0x19f9, 0xd9da };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0x64580000, 0x758378a4, 0xd9da19f9 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0x6458000000000000, 0xd9da19f9758378a4 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0xd9da19f9758378a464580000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0xd9da19f9758378a46458000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_26_1__tab[] = { 0xd9da19f9758378a4645800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x0a00, 0x9828 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x98280a00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x98280a0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x98280a000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x98280a00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_27_0__tab[] = { 0x98280a0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0x5b08, 0xe1bd, 0xe237, 0x7bac, 0xd75b };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0x5b080000, 0xe237e1bd, 0xd75b7bac };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0x5b08000000000000, 0xd75b7bace237e1bd };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0xd75b7bace237e1bd5b080000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0xd75b7bace237e1bd5b08000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_27_1__tab[] = { 0xd75b7bace237e1bd5b0800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0xda00, 0x99d5 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0x99d5da00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0x99d5da0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0x99d5da000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0x99d5da00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_28_0__tab[] = { 0x99d5da0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xdeb8, 0xe8b8, 0x71df, 0xc758, 0xd501 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xdeb80000, 0x71dfe8b8, 0xd501c758 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xdeb8000000000000, 0xd501c75871dfe8b8 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xd501c75871dfe8b8deb80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xd501c75871dfe8b8deb8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_28_1__tab[] = { 0xd501c75871dfe8b8deb800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9600, 0x9b74 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9b749600 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9b74960000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9b7496000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9b749600000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_29_0__tab[] = { 0x9b74960000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xccc8, 0x62b3, 0x9c6c, 0x8315, 0xd2c9 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xccc80000, 0x9c6c62b3, 0xd2c98315 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xccc8000000000000, 0xd2c983159c6c62b3 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xd2c983159c6c62b3ccc80000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xd2c983159c6c62b3ccc8000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_29_1__tab[] = { 0xd2c983159c6c62b3ccc800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x4000, 0x9d05 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x9d054000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x9d05400000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x9d0540000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x9d054000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_30_0__tab[] = { 0x9d05400000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0x3588, 0x1732, 0x5cad, 0xa619, 0xd0af };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0x35880000, 0x5cad1732, 0xd0afa619 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0x3588000000000000, 0xd0afa6195cad1732 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0xd0afa6195cad173235880000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0xd0afa6195cad17323588000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_30_1__tab[] = { 0xd0afa6195cad1732358800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0xc800, 0x9e88 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0x9e88c800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0x9e88c80000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0x9e88c8000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0x9e88c800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_31_0__tab[] = { 0x9e88c80000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xd578, 0xf7ca, 0x63ee, 0x86e6, 0xceb1 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xd5780000, 0x63eef7ca, 0xceb186e6 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xd578000000000000, 0xceb186e663eef7ca };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xceb186e663eef7cad5780000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xceb186e663eef7cad578000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_31_1__tab[] = { 0xceb186e663eef7cad57800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0x0000, 0xa000 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0xa0000000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0xa000000000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0xa00000000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0xa0000000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_32_0__tab[] = { 0xa000000000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccd0, 0xcccc, 0xcccc, 0xcccc, 0xcccc };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccd00000, 0xcccccccc, 0xcccccccc };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccd0000000000000, 0xcccccccccccccccc };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccccccccccccccccccd00000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccccccccccccccccccd0000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_32_1__tab[] = { 0xccccccccccccccccccd000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xae00, 0xa16b };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xa16bae00 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xa16bae0000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xa16bae000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xa16bae00000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_33_0__tab[] = { 0xa16bae0000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0x0888, 0xa187, 0x5304, 0x6404, 0xcaff };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0x08880000, 0x5304a187, 0xcaff6404 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0x0888000000000000, 0xcaff64045304a187 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0xcaff64045304a18708880000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0xcaff64045304a1870888000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_33_1__tab[] = { 0xcaff64045304a187088800000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0x8000, 0xa2cc };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0xa2cc8000 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0xa2cc800000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0xa2cc80000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0xa2cc8000000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_34_0__tab[] = { 0xa2cc800000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xfb50, 0x17ca, 0x5a79, 0x73d8, 0xc947 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xfb500000, 0x5a7917ca, 0xc94773d8 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xfb50000000000000, 0xc94773d85a7917ca };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xc94773d85a7917cafb500000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xc94773d85a7917cafb50000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_34_1__tab[] = { 0xc94773d85a7917cafb5000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0x1800, 0xa423 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0xa4231800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0xa423180000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0xa42318000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0xa4231800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_35_0__tab[] = { 0xa423180000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0x6960, 0x18c2, 0x6037, 0x567c, 0xc7a3 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0x69600000, 0x603718c2, 0xc7a3567c };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0x6960000000000000, 0xc7a3567c603718c2 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0xc7a3567c603718c269600000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0xc7a3567c603718c26960000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_35_1__tab[] = { 0xc7a3567c603718c2696000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0x0800, 0xa570 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0xa5700800 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0xa570080000000000 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0xa57008000000000000000000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0xa5700800000000000000000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_36_0__tab[] = { 0xa570080000000000000000000000000000000000000000000000000000000000 };
#endif

#if 0
#elif BITS_PER_MP_LIMB == 16
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xff10, 0xf9e9, 0xe054, 0x9236, 0xc611 };
#elif BITS_PER_MP_LIMB == 32
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xff100000, 0xe054f9e9, 0xc6119236 };
#elif BITS_PER_MP_LIMB == 64
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xff10000000000000, 0xc6119236e054f9e9 };
#elif BITS_PER_MP_LIMB == 96
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xc6119236e054f9e9ff100000 };
#elif BITS_PER_MP_LIMB == 128
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xc6119236e054f9e9ff10000000000000 };
#elif BITS_PER_MP_LIMB == 256
const mp_limb_t mpfr_l2b_36_1__tab[] = { 0xc6119236e054f9e9ff1000000000000000000000000000000000000000000000 };
#endif

const __mpfr_struct __gmpfr_l2b[BASE_MAX-1][2] = {
  { { 23, 1,  1, (mp_limb_t *) mpfr_l2b_2_0__tab },
    { 77, 1,  1, (mp_limb_t *) mpfr_l2b_2_1__tab } },
  { { 23, 1,  1, (mp_limb_t *) mpfr_l2b_3_0__tab },
    { 77, 1,  0, (mp_limb_t *) mpfr_l2b_3_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_4_0__tab },
    { 77, 1,  0, (mp_limb_t *) mpfr_l2b_4_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_5_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_5_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_6_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_6_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_7_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_7_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_8_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_8_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_9_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_9_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_10_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_10_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_11_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_11_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_12_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_12_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_13_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_13_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_14_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_14_1__tab } },
  { { 23, 1,  2, (mp_limb_t *) mpfr_l2b_15_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_15_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_16_0__tab },
    { 77, 1, -1, (mp_limb_t *) mpfr_l2b_16_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_17_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_17_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_18_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_18_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_19_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_19_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_20_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_20_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_21_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_21_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_22_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_22_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_23_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_23_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_24_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_24_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_25_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_25_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_26_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_26_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_27_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_27_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_28_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_28_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_29_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_29_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_30_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_30_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_31_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_31_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_32_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_32_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_33_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_33_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_34_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_34_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_35_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_35_1__tab } },
  { { 23, 1,  3, (mp_limb_t *) mpfr_l2b_36_0__tab },
    { 77, 1, -2, (mp_limb_t *) mpfr_l2b_36_1__tab } } };

/***************************************************************************/

/* returns ceil(e * log2(b)^((-1)^i)), or ... + 1 */
static mp_exp_t
ceil_mul (mp_exp_t e, int beta, int i)
{
  mpfr_srcptr p;
  mpfr_t t;
  mp_exp_t r;

  p = &__gmpfr_l2b[beta-2][i];
  mpfr_init2 (t, sizeof (mp_exp_t) * CHAR_BIT);
  mpfr_set_exp_t (t, e, GMP_RNDU);
  mpfr_mul (t, t, p, GMP_RNDU);
  r = mpfr_get_exp_t (t, GMP_RNDU);
  mpfr_clear (t);
  return r;
}

/* prints the mantissa of x in the string s, and writes the corresponding
   exponent in e.
   x is rounded with direction rnd, m is the number of digits of the mantissa,
   b is the given base (2 <= b <= 36).

   Return value:
   if s=NULL, allocates a string to store the mantissa, with
   m characters, plus a final '\0', plus a possible minus sign
   (thus m+1 or m+2 characters).

   Important: when you call this function with s=NULL, don't forget to free
   the memory space allocated, with free(s, strlen(s)).
*/
char*
mpfr_get_str (char *s, mp_exp_t *e, int b, size_t m, mpfr_srcptr x, mp_rnd_t rnd)
{
  int exact;                      /* exact result */
  mp_exp_t exp, g;
  mp_exp_t prec; /* precision of the computation */
  long err;
  mp_limb_t *a;
  mp_exp_t exp_a;
  mp_limb_t *result;
  mp_limb_t *xp;
  mp_limb_t *reste;
  size_t nx, nx1;
  size_t n, i;
  char *s0;
  int neg;
  int ret; /* return value of mpfr_get_str_aux */
  MPFR_ZIV_DECL (loop);
  MPFR_SAVE_EXPO_DECL (expo);
  MPFR_TMP_DECL(marker);

  /* if exact = 1 then err is undefined */
  /* otherwise err is such that |x*b^(m-g)-a*2^exp_a| < 2^(err+exp_a) */

  /* is the base valid? */
  if (b < 2 || b > 36)
    return NULL;

  if (MPFR_UNLIKELY (MPFR_IS_NAN (x)))
    {
      if (s == NULL)
        s = (char *) (*__gmp_allocate_func) (6);
      strcpy (s, "@NaN@");
      return s;
    }

  neg = MPFR_SIGN(x) < 0; /* 0 if positive, 1 if negative */

  if (MPFR_UNLIKELY (MPFR_IS_INF (x)))
    {
      if (s == NULL)
        s = (char *) (*__gmp_allocate_func) (neg + 6);
      strcpy (s, (neg) ? "-@Inf@" : "@Inf@");
      return s;
    }

  MPFR_SAVE_EXPO_MARK (expo);  /* needed for ceil_mul (at least) */

  if (m == 0)
    {

      /* take at least 1 + ceil(n*log(2)/log(b)) digits, where n is the
         number of bits of the mantissa, to ensure back conversion from
         the output gives the same floating-point.

         Warning: if b = 2^k, this may be too large. The worst case is when
         the first base-b digit contains only one bit, so we get
         1 + ceil((n-1)/k) = 2 + floor((n-2)/k) instead.
      */
      m = 1 + ceil_mul (IS_POW2(b) ? MPFR_PREC(x) - 1 : MPFR_PREC(x), b, 1);
      if (m < 2)
        m = 2;
    }

  /* the code below for non-power-of-two bases works for m=1 */
  MPFR_ASSERTN (m >= 2 || (IS_POW2(b) == 0 && m >= 1));

  /* x is a floating-point number */

  if (MPFR_IS_ZERO(x))
    {
      if (s == NULL)
        s = (char*) (*__gmp_allocate_func) (neg + m + 1);
      s0 = s;
      if (neg)
        *s++ = '-';
      memset (s, '0', m);
      s[m] = '\0';
      *e = 0; /* a bit like frexp() in ISO C99 */
      MPFR_SAVE_EXPO_FREE (expo);
      return s0; /* strlen(s0) = neg + m */
    }

  if (s == NULL)
    s = (char*) (*__gmp_allocate_func) (neg + m + 1);
  s0 = s;
  if (neg)
    *s++ = '-';

  xp = MPFR_MANT(x);

  if (IS_POW2(b))
    {
      int pow2;
      mp_exp_t f, r;
      mp_limb_t *x1;
      mp_size_t nb;
      int inexp;

      count_leading_zeros (pow2, (mp_limb_t) b);
      pow2 = BITS_PER_MP_LIMB - pow2 - 1; /* base = 2^pow2 */

      /* set MPFR_EXP(x) = f*pow2 + r, 1 <= r <= pow2 */
      f = (MPFR_GET_EXP (x) - 1) / pow2;
      r = MPFR_GET_EXP (x) - f * pow2;
      if (r <= 0)
        {
          f --;
          r += pow2;
        }

      /* the first digit will contain only r bits */
      prec = (m - 1) * pow2 + r; /* total number of bits */
      n = (prec - 1) / BITS_PER_MP_LIMB + 1;

      MPFR_TMP_MARK (marker);
      x1 = (mp_limb_t*) MPFR_TMP_ALLOC((n + 1) * sizeof (mp_limb_t));
      nb = n * BITS_PER_MP_LIMB - prec;
      /* round xp to the precision prec, and put it into x1
         put the carry into x1[n] */
      if ((x1[n] = mpfr_round_raw (x1, xp, MPFR_PREC(x),
                                  MPFR_IS_STRICTNEG(x),
                                   prec, rnd, &inexp)))
        {
          /* overflow when rounding x: x1 = 2^prec */
          if (r == pow2)    /* prec = m * pow2,
                               2^prec will need (m+1) digits in base 2^pow2 */
            {
              /* divide x1 by 2^pow2, and increase the exponent */
              mpn_rshift (x1, x1, n + 1, pow2);
              f ++;
            }
          else /* 2^prec needs still m digits, but x1 may need n+1 limbs */
            n ++;
        }

      /* it remains to shift x1 by nb limbs to the right, since mpn_get_str
         expects a right-normalized number */
      if (nb != 0)
        {
          mpn_rshift (x1, x1, n, nb);
          /* the most significant word may be zero */
          if (x1[n - 1] == 0)
            n --;
        }

      mpn_get_str ((unsigned char*) s, b, x1, n);
      for (i=0; i<m; i++)
        s[i] = num_to_text[(int) s[i]];
      s[m] = 0;

      /* the exponent of s is f + 1 */
      *e = f + 1;

      MPFR_TMP_FREE(marker);
      MPFR_SAVE_EXPO_FREE (expo);
      return (s0);
    }

  /* if x < 0, reduce to x > 0 */
  if (neg)
    rnd = MPFR_INVERT_RND(rnd);

  g = ceil_mul (MPFR_GET_EXP (x) - 1, b, 1);
  exact = 1;
  prec = ceil_mul (m, b, 0) + 1;
  exp = ((mp_exp_t) m < g) ? g - (mp_exp_t) m : (mp_exp_t) m - g;
  prec += MPFR_INT_CEIL_LOG2 (prec); /* number of guard bits */
  if (exp != 0) /* add maximal exponentiation error */
    prec += 3 * (mp_exp_t) MPFR_INT_CEIL_LOG2 (exp);

  MPFR_ZIV_INIT (loop, prec);
  for (;;)
    {
      MPFR_TMP_MARK(marker);

      exact = 1;

      /* number of limbs */
      n = 1 + (prec - 1) / BITS_PER_MP_LIMB;

      /* a will contain the approximation of the mantissa */
      a = (mp_limb_t*) MPFR_TMP_ALLOC (n * sizeof (mp_limb_t));

      nx = 1 + (MPFR_PREC(x) - 1) / BITS_PER_MP_LIMB;

      if ((mp_exp_t) m == g) /* final exponent is 0, no multiplication or
                                division to perform */
        {
          if (nx > n)
            exact = mpn_scan1 (xp, 0) >= (nx - n) * BITS_PER_MP_LIMB;
          err = !exact;
          MPN_COPY2 (a, n, xp, nx);
          exp_a = MPFR_GET_EXP (x) - n * BITS_PER_MP_LIMB;
        }
      else if ((mp_exp_t) m > g) /* we have to multiply x by b^exp */
        {
          mp_limb_t *x1;

          /* a2*2^exp_a =  b^e */
          err = mpfr_mpn_exp (a, &exp_a, b, exp, n);
          /* here, the error on a is at most 2^err ulps */
          exact = (err == -1);

          /* x = x1*2^(n*BITS_PER_MP_LIMB) */
          x1 = (nx >= n) ? xp + nx - n : xp;
          nx1 = (nx >= n) ? n : nx; /* nx1 = min(n, nx) */

          /* test si exact */
          if (nx > n)
            exact = (exact &&
                     ((mpn_scan1 (xp, 0) >= (nx - n) * BITS_PER_MP_LIMB)));

          /* we loose one more bit in the multiplication,
             except when err=0 where we loose two bits */
          err = (err <= 0) ? 2 : err + 1;

          /* result = a * x */
          result = (mp_limb_t*) MPFR_TMP_ALLOC ((n + nx1) * sizeof (mp_limb_t));
          mpn_mul (result, a, n, x1, nx1);
          exp_a += MPFR_GET_EXP (x);
          if (mpn_scan1 (result, 0) < (nx1 * BITS_PER_MP_LIMB))
            exact = 0;

          /* normalize a and truncate */
          if ((result[n + nx1 - 1] & MPFR_LIMB_HIGHBIT) == 0)
            {
              mpn_lshift (a, result + nx1, n , 1);
              a[0] |= result[nx1 - 1] >> (BITS_PER_MP_LIMB - 1);
              exp_a --;
            }
          else
            MPN_COPY (a, result + nx1, n);
        }
      else
        {
          mp_limb_t *x1;

          /* a2*2^exp_a =  b^e */
          err = mpfr_mpn_exp (a, &exp_a, b, exp, n);
          exact = (err == -1);

          /* allocate memory for x1, result and reste */
          x1 = (mp_limb_t*) MPFR_TMP_ALLOC (2 * n * sizeof (mp_limb_t));
          result = (mp_limb_t*) MPFR_TMP_ALLOC ((n + 1) * sizeof (mp_limb_t));
          reste = (mp_limb_t*) MPFR_TMP_ALLOC (n * sizeof (mp_limb_t));

          /* initialize x1 = x */
          MPN_COPY2 (x1, 2 * n, xp, nx);
          if ((exact) && (nx > 2 * n) &&
              (mpn_scan1 (xp, 0) < (nx - 2 * n) * BITS_PER_MP_LIMB))
            exact = 0;

          /* result = x / a */
          mpn_tdiv_qr (result, reste, 0, x1, 2 * n, a, n);
          exp_a = MPFR_GET_EXP (x) - exp_a - 2 * n * BITS_PER_MP_LIMB;

          /* test if division was exact */
          if (exact)
            exact = mpn_popcount (reste, n) == 0;

          /* normalize the result and copy into a */
          if (result[n] == 1)
            {
              mpn_rshift (a, result, n, 1);
              a[n - 1] |= MPFR_LIMB_HIGHBIT;;
              exp_a ++;
            }
          else
            MPN_COPY (a, result, n);

          err = (err == -1) ? 2 : err + 2;
        }

      /* check if rounding is possible */
      if (exact)
        err = -1;
      ret = mpfr_get_str_aux (s, e, a, n, exp_a, err, b, m, rnd);
      if (ret == MPFR_ROUND_FAILED)
        {
          /* too large error: increment the working precision */
          MPFR_ZIV_NEXT (loop, prec);
        }
      else if (ret == -MPFR_ROUND_FAILED)
        {
          /* too many digits in mantissa: exp = |m-g| */
          if ((mp_exp_t) m > g) /* exp = m - g, multiply by b^exp */
            {
              g++;
              exp --;
            }
          else /* exp = g - m, divide by b^exp */
            {
              g++;
              exp ++;
            }
        }
      else
        break;

      MPFR_TMP_FREE(marker);
    }
  MPFR_ZIV_FREE (loop);

  *e += g;

  MPFR_TMP_FREE(marker);
  MPFR_SAVE_EXPO_FREE (expo);
  return s0;
}

void mpfr_free_str (char *str)
{
   (*__gmp_free_func) (str, strlen (str) + 1);
}
