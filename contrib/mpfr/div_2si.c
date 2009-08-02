/* mpfr_div_2si -- divide a floating-point number by a power of two

Copyright 1999, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

int
mpfr_div_2si (mpfr_ptr y, mpfr_srcptr x, long int n, mp_rnd_t rnd_mode)
{
  int inexact;

  MPFR_LOG_FUNC (("x[%#R]=%R n=%ld rnd=%d", x, x, n, rnd_mode),
                 ("y[%#R]=%R inexact=%d", y, y, inexact));

  inexact = MPFR_UNLIKELY(y != x) ? mpfr_set (y, x, rnd_mode) : 0;

  if (MPFR_LIKELY( MPFR_IS_PURE_FP(y) ))
    {
      mp_exp_t exp = MPFR_GET_EXP (y);
      if (MPFR_UNLIKELY( n > 0 && (__gmpfr_emin > MPFR_EMAX_MAX - n ||
                                   exp < __gmpfr_emin + n)) )
        {
          if (rnd_mode == GMP_RNDN &&
              (__gmpfr_emin > MPFR_EMAX_MAX - (n - 1) ||
               exp < __gmpfr_emin + (n - 1) ||
               (inexact >= 0 && mpfr_powerof2_raw (y))))
            rnd_mode = GMP_RNDZ;
          return mpfr_underflow (y, rnd_mode, MPFR_SIGN(y));
        }

      if (MPFR_UNLIKELY(n < 0 && (__gmpfr_emax < MPFR_EMIN_MIN - n ||
                                  exp > __gmpfr_emax + n)) )
        return mpfr_overflow (y, rnd_mode, MPFR_SIGN(y));

      MPFR_SET_EXP (y, exp - n);
    }

  return inexact;
}
