/* mpfr_div_2ui -- divide a floating-point number by a power of two

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
mpfr_div_2ui (mpfr_ptr y, mpfr_srcptr x, unsigned long n, mp_rnd_t rnd_mode)
{
  int inexact;

  MPFR_LOG_FUNC (("x[%#R]=%R n=%lu rnd=%d", x, x, n, rnd_mode),
                 ("y[%#R]=%R inexact=%d", y, y, inexact));

  /* Most of the times, this function is called with y==x */
  inexact = MPFR_UNLIKELY(y != x) ? mpfr_set (y, x, rnd_mode) : 0;

  if (MPFR_LIKELY( MPFR_IS_PURE_FP(y)) )
    {
      /* n will have to be casted to long to make sure that the addition
         and subtraction below (for overflow detection) are signed */
      while (MPFR_UNLIKELY(n > LONG_MAX))
        {
          int inex2;

          n -= LONG_MAX;
          inex2 = mpfr_div_2ui(y, y, LONG_MAX, rnd_mode);
          if (inex2)
            return inex2; /* underflow */
        }

      /* MPFR_EMAX_MAX - (long) n is signed and doesn't lead to an integer
         overflow; the first test useful so that the real test can't lead
         to an integer overflow. */
      {
        mp_exp_t exp = MPFR_GET_EXP (y);
        if (MPFR_UNLIKELY( __gmpfr_emin > MPFR_EMAX_MAX - (long) n ||
                           exp < __gmpfr_emin + (long) n) )
          {
            if (rnd_mode == GMP_RNDN &&
                (__gmpfr_emin > MPFR_EMAX_MAX - (long) (n - 1) ||
                 exp < __gmpfr_emin + (long) (n - 1) ||
                 (inexact >= 0 && mpfr_powerof2_raw (y))))
              rnd_mode = GMP_RNDZ;
            return mpfr_underflow (y, rnd_mode, MPFR_SIGN(y));
          }

        MPFR_SET_EXP(y, exp - (long) n);
      }
    }

  return inexact;
}
