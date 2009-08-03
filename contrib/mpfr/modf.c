/* mpfr_modf -- Integral and fractional part.

Copyright 2007, 2008, 2009 Free Software Foundation, Inc.
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

/* Set iop to the integral part of op and fop to its fractional part */
int
mpfr_modf (mpfr_ptr iop, mpfr_ptr fop, mpfr_srcptr op, mpfr_rnd_t rnd_mode)
{
  mp_exp_t ope;
  mp_prec_t opq;
  int inexact;
  MPFR_SAVE_EXPO_DECL (expo);

  MPFR_LOG_FUNC (("op[%#R]=%R rnd=%d", op, op, rnd_mode),
                 ("iop[%#R]=%R fop[%#R]=%R", iop, iop, fop, fop));

  MPFR_ASSERTN (iop != fop);

  if ( MPFR_UNLIKELY (MPFR_IS_SINGULAR (op)) )
    {
      if (MPFR_IS_NAN (op))
        {
          MPFR_SET_NAN (iop);
          MPFR_SET_NAN (fop);
          MPFR_RET_NAN;
        }
      MPFR_SET_SAME_SIGN (iop, op);
      MPFR_SET_SAME_SIGN (fop, op);
      if (MPFR_IS_INF (op))
        {
          MPFR_SET_INF (iop);
          MPFR_SET_ZERO (fop);
          MPFR_RET (0);
        }
      else /* op is zero */
        {
          MPFR_ASSERTD (MPFR_IS_ZERO (op));
          MPFR_SET_ZERO (iop);
          MPFR_SET_ZERO (fop);
          MPFR_RET (0);
        }
    }

  MPFR_SAVE_EXPO_MARK (expo);

  ope = MPFR_GET_EXP (op);
  opq = MPFR_PREC (op);

  if (ope <=0)   /* 0 < |op| < 1 */
    {
      inexact = (fop != op) ? mpfr_set (fop, op, rnd_mode) : 0;
      MPFR_SET_SAME_SIGN (iop, op);
      MPFR_SET_ZERO (iop);
      MPFR_SAVE_EXPO_FREE (expo);
      mpfr_check_range (fop, inexact, rnd_mode); /* set the underflow flag if needed */
      MPFR_RET (MPFR_INT_SIGN (op) > 0 ? -2 : +2);
    }
  else if (ope >= opq) /* op has no fractional part */
    {
      inexact = (iop != op)? mpfr_set (iop, op, rnd_mode) : 0;
      MPFR_SET_SAME_SIGN (fop, op);
      MPFR_SET_ZERO (fop);
      MPFR_SAVE_EXPO_FREE (expo);
      return mpfr_check_range (iop, inexact, rnd_mode); /* set the overflow flag if needed */
    }
  else /* op has both integral and fractional parts */
    {
      int inexi, inexf;
      mpfr_t opf, opi;

      /* opi and opf are set with minimal but sufficient precision */
      mpfr_init2 (opi, ope <= MPFR_PREC_MIN ? MPFR_PREC_MIN : ope);
      inexi = mpfr_trunc (opi, op);
      mpfr_init2 (opf, opq - ope <= MPFR_PREC_MIN ? MPFR_PREC_MIN : opq - ope);
      inexf = mpfr_frac (opf, op, GMP_RNDZ);
      MPFR_ASSERTD (inexf == 0);

      inexf = mpfr_set (fop, opf, rnd_mode);
      inexi = mpfr_set (iop, opi, rnd_mode);
      mpfr_clear (opi);
      mpfr_clear (opf);

      MPFR_SAVE_EXPO_FREE (expo);
      inexf = mpfr_check_range (fop, inexf, rnd_mode);
      inexi = mpfr_check_range (iop, inexi, rnd_mode);

      /* return value like mpfr_trunc():   */
      /* 0 iff iop and fop are exact       */
      /* -1 if op is an integer, op > iop    */
      /* +1 if op is an integer, op < iop    */
      /* -2 if op is not an integer, op > 0  */
      /* +2 if op is not an integer, op < 0  */
      inexact = inexf ? (inexi ? 2 * inexi : -2 * MPFR_INT_SIGN (op)) :
        (mpfr_zero_p (fop) ? inexi : 2 * inexi);
      MPFR_RET (inexact);
    }
}
