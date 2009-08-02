/* mpfr_round_p -- check if an approximation is roundable.

Copyright 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

#include "mpfr-impl.h"

/* Check against mpfr_can_round ? */
#ifdef WANT_ASSERT
# if WANT_ASSERT >= 2
int mpfr_round_p_2 (mp_limb_t *, mp_size_t, mp_exp_t, mp_prec_t);
int
mpfr_round_p (mp_limb_t *bp, mp_size_t bn, mp_exp_t err0, mp_prec_t prec)
{
  int i1, i2;

  i1 = mpfr_round_p_2 (bp, bn, err0, prec);
  i2 = mpfr_can_round_raw (bp, bn, MPFR_SIGN_POS, err0,
                           GMP_RNDN, GMP_RNDZ, prec);
  if (i1 != i2)
    {
      fprintf (stderr, "mpfr_round_p(%d) != mpfr_can_round(%d)!\n"
               "bn = %lu, err0 = %ld, prec = %lu\nbp = ", i1, i2,
               (unsigned long) bn, (long) err0, (unsigned long) prec);
      gmp_fprintf (stderr, "%NX\n", bp, bn);
      MPFR_ASSERTN (0);
    }
  return i1;
}
# define mpfr_round_p mpfr_round_p_2
# endif
#endif

/*
 * Assuming {bp, bn} is an approximation of a non-singular number
 * with error at most equal to 2^(EXP(b)-err0) (`err0' bits of b are known)
 * of direction unknown, check if we can round b toward zero with
 * precision prec.
 */
int
mpfr_round_p (mp_limb_t *bp, mp_size_t bn, mp_exp_t err0, mp_prec_t prec)
{
  mp_prec_t err;
  mp_size_t k, n;
  mp_limb_t tmp, mask;
  int s;

  err = (mp_prec_t) bn * BITS_PER_MP_LIMB;
  if (MPFR_UNLIKELY (err0 <= 0 || (mpfr_uexp_t) err0 <= prec || prec >= err))
    return 0;  /* can't round */
  err = MIN (err, (mpfr_uexp_t) err0);

  k = prec / BITS_PER_MP_LIMB;
  s = BITS_PER_MP_LIMB - prec%BITS_PER_MP_LIMB;
  n = err / BITS_PER_MP_LIMB - k;

  MPFR_ASSERTD (n >= 0);
  MPFR_ASSERTD (bn > k);

  /* Check first limb */
  bp += bn-1-k;
  tmp = *bp--;
  mask = s == BITS_PER_MP_LIMB ? MP_LIMB_T_MAX : MPFR_LIMB_MASK (s);
  tmp &= mask;

  if (MPFR_LIKELY (n == 0))
    {
      /* prec and error are in the same limb */
      s = BITS_PER_MP_LIMB - err % BITS_PER_MP_LIMB;
      MPFR_ASSERTD (s < BITS_PER_MP_LIMB);
      tmp  >>= s;
      mask >>= s;
      return tmp != 0 && tmp != mask;
    }
  else if (MPFR_UNLIKELY (tmp == 0))
    {
      /* Check if all (n-1) limbs are 0 */
      while (--n)
        if (*bp-- != 0)
          return 1;
      /* Check if final error limb is 0 */
      s = BITS_PER_MP_LIMB - err % BITS_PER_MP_LIMB;
      if (s == BITS_PER_MP_LIMB)
        return 0;
      tmp = *bp >> s;
      return tmp != 0;
    }
  else if (MPFR_UNLIKELY (tmp == mask))
    {
      /* Check if all (n-1) limbs are 11111111111111111 */
      while (--n)
        if (*bp-- != MP_LIMB_T_MAX)
          return 1;
      /* Check if final error limb is 0 */
      s = BITS_PER_MP_LIMB - err % BITS_PER_MP_LIMB;
      if (s == BITS_PER_MP_LIMB)
        return 0;
      tmp = *bp >> s;
      return tmp != (MP_LIMB_T_MAX >> s);
    }
  else
    {
      /* First limb is different from 000000 or 1111111 */
      return 1;
    }
}
