/* mpfr_fits_*_p -- test whether an mpfr fits a C signed type.

Copyright 2003, 2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
Contributed by the Arenaire and Cacao projects, INRIA.
Copied from mpf/fits_s.h.

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
FUNCTION (mpfr_srcptr f, mp_rnd_t rnd)
{
  mp_exp_t exp;
  mp_prec_t prec;
  TYPE s;
  mpfr_t x;
  int neg;
  int res;

  if (MPFR_UNLIKELY (MPFR_IS_SINGULAR (f)))
    /* Zero always fit */
    return MPFR_IS_ZERO (f) ? 1 : 0;

  /* now it fits if either
     (a) MINIMUM <= f <= MAXIMUM
     (b) or MINIMUM <= round(f, prec(slong), rnd) <= MAXIMUM */

  exp = MPFR_GET_EXP (f);
  if (exp < 1)
    return 1; /* |f| < 1: always fits */

  neg = MPFR_IS_NEG (f);

  /* let EXTREMUM be MAXIMUM if f > 0, and MINIMUM if f < 0 */

  /* first compute prec(EXTREMUM), this could be done at configure time */
  s = (neg) ? MINIMUM : MAXIMUM;
  for (prec = 0; s != 0; s /= 2, prec ++);

  /* EXTREMUM needs prec bits, i.e. 2^(prec-1) <= |EXTREMUM| < 2^prec */

   /* if exp < prec - 1, then f < 2^(prec-1) < |EXTREMUM| */
  if ((mpfr_prec_t) exp < prec - 1)
    return 1;

  /* if exp > prec + 1, then f >= 2^prec > EXTREMUM */
  if ((mpfr_prec_t) exp > prec + 1)
    return 0;

  /* remains cases exp = prec-1 to prec+1 */

  /* hard case: first round to prec bits, then check */
  mpfr_init2 (x, prec);
  mpfr_set (x, f, rnd);
  res = (neg) ? (mpfr_cmp_si (x, MINIMUM) >= 0)
    : (mpfr_cmp_si (x, MAXIMUM) <= 0);
  mpfr_clear (x);

  return res;
}

