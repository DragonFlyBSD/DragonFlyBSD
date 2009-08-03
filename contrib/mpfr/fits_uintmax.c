/* mpfr_fits_uintmax_p -- test whether an mpfr fits an uintmax_t.

Copyright 2004, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

#ifdef HAVE_CONFIG_H
# include "config.h"            /* for a build within gmp */
#endif

/* The ISO C99 standard specifies that in C++ implementations the
   INTMAX_MAX, ... macros should only be defined if explicitly requested.  */
#if defined __cplusplus
# define __STDC_LIMIT_MACROS
# define __STDC_CONSTANT_MACROS
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h> /* for intmax_t */
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include "mpfr-impl.h"

#ifdef _MPFR_H_HAVE_INTMAX_T

/* We can't use fits_u.h <= mpfr_cmp_ui */
int
mpfr_fits_uintmax_p (mpfr_srcptr f, mp_rnd_t rnd)
{
  mp_exp_t exp;
  mp_prec_t prec;
  uintmax_t s;
  mpfr_t x, y;
  int res;

  if (MPFR_IS_NAN(f) || MPFR_IS_INF(f) || MPFR_SIGN(f) < 0)
    return 0; /* does not fit */

  if (MPFR_IS_ZERO(f))
    return 1; /* zero always fits */

  /* now it fits if
     (a) f <= MAXIMUM
     (b) round(f, prec(slong), rnd) <= MAXIMUM */

  exp = MPFR_EXP(f);
  if (exp < 1)
    return 1; /* |f| < 1: always fits */

  /* first compute prec(MAXIMUM) */
  for (s = UINTMAX_MAX, prec = 0; s != 0; s /= 2, prec ++);

  /* MAXIMUM needs prec bits, i.e. 2^(prec-1) <= |MAXIMUM| < 2^prec */

   /* if exp < prec - 1, then f < 2^(prec-1) < |MAXIMUM| */
  if ((mpfr_prec_t) exp < prec - 1)
    return 1;

  /* if exp > prec + 1, then f >= 2^prec > MAXIMUM */
  if ((mpfr_prec_t) exp > prec + 1)
    return 0;

  /* remains cases exp = prec-1 to prec+1 */

  /* hard case: first round to prec bits, then check */
  mpfr_init2 (x, prec);
  mpfr_init2 (y, prec);
  mpfr_set (x, f, rnd);
  res = mpfr_set_uj (y, UINTMAX_MAX, GMP_RNDN);
  MPFR_ASSERTD (res == 0);
  res = mpfr_cmp (x, y) <= 0;
  mpfr_clear (y);
  mpfr_clear (x);

  return res;
}

#endif
