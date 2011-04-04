/* mpfr_get_z_exp -- get a multiple-precision integer and an exponent
                     from a floating-point number

Copyright 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

/* puts the significand of f into z, and returns 'exp' such that f = z * 2^exp
 *
 * 0 doesn't have an exponent, therefore the returned exponent in this case
 * isn't really important. We choose to return __gmpfr_emin because
 *   1) it is in the exponent range [__gmpfr_emin,__gmpfr_emax],
 *   2) the smaller a number is (in absolute value), the smaller its
 *      exponent is. In other words, the f -> exp function is monotonous
 *      on nonnegative numbers. --> This is WRONG since the returned
 *      exponent is not necessarily in the exponent range!
 * Note that this is different from the C function frexp().
 */

mp_exp_t
mpfr_get_z_exp (mpz_ptr z, mpfr_srcptr f)
{
  mp_size_t fn;
  int sh;

  MPFR_ASSERTD (MPFR_IS_FP (f));

  if (MPFR_UNLIKELY (MPFR_IS_ZERO (f)))
    {
      mpz_set_ui (z, 0);
      return __gmpfr_emin;
    }

  fn = MPFR_LIMB_SIZE(f);

  /* check whether allocated space for z is enough */
  if (MPFR_UNLIKELY (ALLOC (z) < fn))
    MPZ_REALLOC (z, fn);

  MPFR_UNSIGNED_MINUS_MODULO (sh, MPFR_PREC (f));
  if (MPFR_LIKELY (sh))
    mpn_rshift (PTR (z), MPFR_MANT (f), fn, sh);
  else
    MPN_COPY (PTR (z), MPFR_MANT (f), fn);

  SIZ(z) = MPFR_IS_NEG (f) ? -fn : fn;

  /* Test if the result is representable. Later, we could choose
     to return MPFR_EXP_MIN if it isn't, or perhaps MPFR_EXP_MAX
     to signal an error. The significand would still be meaningful. */
  MPFR_ASSERTD ((mp_exp_unsigned_t) MPFR_GET_EXP (f) - MPFR_EXP_MIN
                >= (mp_exp_unsigned_t) MPFR_PREC(f));

  return MPFR_GET_EXP (f) - MPFR_PREC (f);
}
