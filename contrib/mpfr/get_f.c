/* mpfr_get_f -- convert a MPFR number to a GNU MPF number

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

/* return value is 0 iff no error occurred in the conversion
   (1 for NaN, +Inf, -Inf that have no equivalent in mpf)
*/
int
mpfr_get_f (mpf_ptr x, mpfr_srcptr y, mp_rnd_t rnd_mode)
{
  mp_size_t sx, sy;
  mp_prec_t precx, precy;
  mp_limb_t *xp;
  int sh;

  if (MPFR_UNLIKELY(MPFR_IS_SINGULAR(y)))
    {
      if (MPFR_IS_ZERO(y))
        {
          mpf_set_ui (x, 0);
          return 0;
        }
      else /* NaN or Inf */
        return 1;
    }

  sx = PREC(x); /* number of limbs of the mantissa of x */

  precy = MPFR_PREC(y);
  precx = (mp_prec_t) sx * BITS_PER_MP_LIMB;
  sy = MPFR_LIMB_SIZE (y);

  xp = PTR (x);

  /* since mpf numbers are represented in base 2^BITS_PER_MP_LIMB,
     we loose -EXP(y) % BITS_PER_MP_LIMB bits in the most significant limb */
  sh = MPFR_GET_EXP(y) % BITS_PER_MP_LIMB;
  sh = sh <= 0 ? - sh : BITS_PER_MP_LIMB - sh;
  MPFR_ASSERTD (sh >= 0);
  if (precy + sh <= precx) /* we can copy directly */
    {
      mp_size_t ds;

      MPFR_ASSERTN (sx >= sy);
      ds = sx - sy;

      if (sh != 0)
        {
          mp_limb_t out;
          out = mpn_rshift (xp + ds, MPFR_MANT(y), sy, sh);
          MPFR_ASSERTN (ds > 0 || out == 0);
          if (ds > 0)
            xp[--ds] = out;
        }
      else
        MPN_COPY (xp + ds, MPFR_MANT (y), sy);
      if (ds > 0)
        MPN_ZERO (xp, ds);
      EXP(x) = (MPFR_GET_EXP(y) + sh) / BITS_PER_MP_LIMB;
    }
  else /* we have to round to precx - sh bits */
    {
      mpfr_t z;
      mp_size_t sz, ds;

      /* Recall that precx = (mp_prec_t) sx * BITS_PER_MP_LIMB */
      mpfr_init2 (z, precx - sh);
      sz = MPFR_LIMB_SIZE (z);
      mpfr_set (z, y, rnd_mode);
      /* warning, sh may change due to rounding, but then z is a power of two,
         thus we can safely ignore its last bit which is 0 */
      sh = MPFR_GET_EXP(z) % BITS_PER_MP_LIMB;
      sh = sh <= 0 ? - sh : BITS_PER_MP_LIMB - sh;
      MPFR_ASSERTD (sx >= sz);
      ds = sx - sz;
      MPFR_ASSERTD (sh >= 0 && ds <= 1);
      if (sh != 0)
        {
          mp_limb_t out;
          out = mpn_rshift (xp + ds, MPFR_MANT(z), sz, sh);
          /* If sh hasn't changed, it is the number of the non-significant
             bits in the lowest limb of z. Therefore out == 0. */
          MPFR_ASSERTD (out == 0);
        }
      else
        MPN_COPY (xp + ds, MPFR_MANT(z), sz);
      if (ds != 0)
        xp[0] = 0;
      EXP(x) = (MPFR_GET_EXP(z) + sh) / BITS_PER_MP_LIMB;
      mpfr_clear (z);
    }

  /* set size and sign */
  SIZ(x) = (MPFR_FROM_SIGN_TO_INT(MPFR_SIGN(y)) < 0) ? -sx : sx;

  return 0;
}
