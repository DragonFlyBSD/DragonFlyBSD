/* mpfr_fac_ui -- factorial of a non-negative integer

Copyright 2001, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

 /* The computation of n! is done by

    n!=prod^{n}_{i=1}i
 */

/* FIXME: efficient problems with large arguments; see comments in gamma.c. */

int
mpfr_fac_ui (mpfr_ptr y, unsigned long int x, mp_rnd_t rnd_mode)
{
  mpfr_t t;       /* Variable of Intermediary Calculation*/
  unsigned long i;
  int round, inexact;

  mp_prec_t Ny;   /* Precision of output variable */
  mp_prec_t Nt;   /* Precision of Intermediary Calculation variable */
  mp_prec_t err;  /* Precision of error */

  mp_rnd_t rnd;
  MPFR_SAVE_EXPO_DECL (expo);
  MPFR_ZIV_DECL (loop);

  /***** test x = 0  and x == 1******/
  if (MPFR_UNLIKELY (x <= 1))
    return mpfr_set_ui (y, 1, rnd_mode); /* 0! = 1 and 1! = 1 */

  MPFR_SAVE_EXPO_MARK (expo);

  /* Initialisation of the Precision */
  Ny = MPFR_PREC (y);

  /* compute the size of intermediary variable */
  Nt = Ny + 2 * MPFR_INT_CEIL_LOG2 (x) + 7;

  mpfr_init2 (t, Nt); /* initialise of intermediary variable */

  rnd = GMP_RNDZ;
  MPFR_ZIV_INIT (loop, Nt);
  for (;;)
    {
      /* compute factorial */
      inexact = mpfr_set_ui (t, 1, rnd);
      for (i = 2 ; i <= x ; i++)
        {
          round = mpfr_mul_ui (t, t, i, rnd);
          /* assume the first inexact product gives the sign
             of difference: is that always correct? */
          if (inexact == 0)
            inexact = round;
        }

      err = Nt - 1 - MPFR_INT_CEIL_LOG2 (Nt);

      round = !inexact || mpfr_can_round (t, err, rnd, GMP_RNDZ,
                                          Ny + (rnd_mode == GMP_RNDN));

      if (MPFR_LIKELY (round))
        {
          /* If inexact = 0, then t is exactly x!, so round is the
             correct inexact flag.
             Otherwise, t != x! since we rounded to zero or away. */
          round = mpfr_set (y, t, rnd_mode);
          if (inexact == 0)
            {
              inexact = round;
              break;
            }
          else if ((inexact < 0 && round <= 0)
                   || (inexact > 0 && round >= 0))
            break;
          else /* inexact and round have opposite signs: we cannot
                  compute the inexact flag. Restart using the
                  symmetric rounding. */
            rnd = (rnd == GMP_RNDZ) ? GMP_RNDU : GMP_RNDZ;
        }
      MPFR_ZIV_NEXT (loop, Nt);
      mpfr_set_prec (t, Nt);
    }
  MPFR_ZIV_FREE (loop);

  mpfr_clear (t);
  MPFR_SAVE_EXPO_FREE (expo);
  return mpfr_check_range (y, inexact, rnd_mode);
}




