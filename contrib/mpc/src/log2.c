/* mpc_log2 -- base-2 logarithm of a complex number.

Copyright (C) 2012, 2020, 2024, 2025, 2026 INRIA

This file is part of GNU MPC.

GNU MPC is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

GNU MPC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this program. If not, see http://logw.gnu.org/licenses/ .
*/

#include <limits.h> /* for CHAR_BIT */
#include "mpc-impl.h"

/* this file was copied from log10.c */

/* return non-zero if |x|=2^k */
static int
is_power_of_two (mpfr_srcptr x)
{
  return mpfr_min_prec (x) == 1;
}

int
mpc_log2 (mpc_ptr rop, mpc_srcptr op, mpc_rnd_t rnd)
{
   int ok = 0, loop = 0, special_re, special_im,
       inex, inex_re, inex_im;
   mpfr_prec_t prec;
   mpfr_t log2;
   mpc_t log;
   mpfr_exp_t saved_emin, saved_emax;

   saved_emin = mpfr_get_emin ();
   saved_emax = mpfr_get_emax ();
   mpfr_set_emin (mpfr_get_emin_min ());
   mpfr_set_emax (mpfr_get_emax_max ());

   mpfr_init2 (log2, 2);
   mpc_init2 (log, 2);
   prec = MPC_MAX_PREC (rop);
   /* compute log(op)/log(2) */
   while (ok == 0) {
      MPC_LOOP_NEXT(loop, op, rop);
      prec += (loop <= 2) ? mpc_ceil_log2 (prec) + 4 : prec / 2;
      mpfr_set_prec (log2, prec);
      mpc_set_prec (log, prec);

      inex = mpc_log (log, op, MPFR_RNDN);
      /* error <= k1 = 1/2 ulp for the real and the imaginary part */

      if (!mpfr_number_p (mpc_imagref (log)) || mpfr_zero_p (mpc_imagref (log))) {
         /* no need to divide by log(2) */
         special_im = 1;
         ok = 1;
      }
      else {
         special_im = 0;
         mpfr_const_log2 (log2, MPFR_RNDN);
         /* error <= k2 = 1/2 ulp */
         mpfr_div (mpc_imagref (log), mpc_imagref (log), log2, MPFR_RNDN);
         /* by the generic error of division
            (eq:proprealdiv in algorithms.tex), +1/2 ulp for final rounding:
            error <= (2 (k1+k2) / (1 - k2 2^(1-prec))) + 1/2 ulp
                  <= 4 ulp for prec >= 2 */
         ok = mpfr_can_round (mpc_imagref (log), prec - 2,
                  MPFR_RNDN, MPFR_RNDZ,
                  MPC_PREC_IM(rop) + (MPC_RND_IM (rnd) == MPFR_RNDN));
      }

      if (ok) {
         if (!mpfr_number_p (mpc_realref (log)) || mpfr_zero_p (mpc_realref (log)))
            special_re = 1;
         else {
            special_re = 0;
            if (special_im)
               /* log2 not yet computed */
              mpfr_const_log2 (log2, MPFR_RNDN);
            mpfr_div (mpc_realref (log), mpc_realref (log), log2, MPFR_RNDN);
            /* same error analysis as above */
            ok = mpfr_can_round (mpc_realref (log), prec - 2,
                     MPFR_RNDN, MPFR_RNDZ,
                     MPC_PREC_RE(rop) + (MPC_RND_RE (rnd) == MPFR_RNDN));
         }
         
         /* Special code to deal with cases where the real part of log2(x+i*y)
            is exact, like x=y=1. Since Re(log2(x+i*y)) = log2(x^2+y^2)/2
            this happens whenever x^2+y^2 is a power of 2.
            Without loss of generality we can assume x = u/2^e and y = v/2^e
            with u, v, e integers. We obtain u^2+v^2 = 2^s for s integer.
            The only solutions are (up to sign):
            (a) u=v=s^((s-1)/2) for s odd >= 1
            (b) u=0 and v=2^(s/2) or u=2^(s/2) and v=0 for s even >= 0.
            Indeed, for s < 0 there is no solution.
            For s=0 the only solutions are (u,v)=(0,1) and (1,0).
            For s=1 the only solution is (u,v)=(1,1).
            For s>=2, since 2^s = 0 mod 4, the sum u^2+v^2 must be divisible by 4;
            the only solution is when both u and v are even, since u^2+v^2 = 1 mod 4
            if exactly one is odd, and u^2+v^2 = 2 mod 4 if both are odd.
            Thus by induction u'=u/2 and v'=v/2 are solutions of u'^2+v'^2 = 2^(s-2).
         */
         if (!ok) {
           if (mpfr_zero_p (mpc_realref (op)) && is_power_of_two (mpc_imagref (op))) {
             /* x=0 and y=2^k */
             mpfr_exp_t ey = mpfr_get_exp (mpc_imagref (op));
             /* |y| = 2^(ey-1) thus log2(x^2+y^2)/2 = ey-1 */
             ok = mpfr_set_si (mpc_realref (log), ey - 1, MPFR_RNDN) == 0;
             /* if the conversion is not exact, because the working precision is
                too small, it will be exact for a larger working precision */
           }
           else if (mpfr_zero_p (mpc_imagref (op)) && is_power_of_two (mpc_realref (op))) {
             /* x=2^k and y=0 */
             mpfr_exp_t ex = mpfr_get_exp (mpc_realref (op));
             /* |x| = 2^(ex-1) thus log2(x^2+y^2)/2 = ex-1 */
             ok = mpfr_set_si (mpc_realref (log), ex - 1, MPFR_RNDN) == 0;
             /* if the conversion is not exact, because the working precision is
                too small, it will be exact for a larger working precision */
           }
           else if (mpfr_cmpabs (mpc_realref (op), mpc_imagref (op)) == 0
                    && is_power_of_two (mpc_realref (op))) {
             mpfr_exp_t ex = mpfr_get_exp (mpc_realref (op));
             /* |x| = |y| = 2^(ex-1) thus log2(x^2+y^2)/2 = (2 ex - 1) / 2 */
             ok = (mpfr_set_si (mpc_realref (log), ex, MPFR_RNDN) == 0)
                  && (mpfr_mul_2ui (mpc_realref (log), mpc_realref (log), 1,
                                      MPFR_RNDN) == 0)
                  && (mpfr_sub_ui (mpc_realref (log), mpc_realref (log), 1,
                                     MPFR_RNDN) == 0)
                  && (mpfr_div_2ui (mpc_realref (log), mpc_realref (log), 1,
                                    MPFR_RNDN) == 0);
             /* if one operation is not exact, because the working precision is
                too small, it will be exact for a larger working precision */
           }
         }
      }
   }

   inex_re = mpfr_set (mpc_realref(rop), mpc_realref (log), MPC_RND_RE (rnd));
   if (special_re)
      inex_re = MPC_INEX_RE (inex);
      /* recover flag from call to mpc_log above */
   inex_im = mpfr_set (mpc_imagref(rop), mpc_imagref (log), MPC_RND_IM (rnd));
   if (special_im)
      inex_im = MPC_INEX_IM (inex);
   mpfr_clear (log2);
   mpc_clear (log);

   /* restore the exponent range, and check the range of results */
   mpfr_set_emin (saved_emin);
   mpfr_set_emax (saved_emax);
   inex_re = mpfr_check_range (mpc_realref (rop), inex_re, MPC_RND_RE (rnd));
   inex_im = mpfr_check_range (mpc_imagref (rop), inex_im, MPC_RND_IM (rnd));

   return MPC_INEX(inex_re, inex_im);
}
