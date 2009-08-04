/* Compute {up,n}^(-1) mod 2(n*GMP_NUMB_BITS).

   Contributed to the GNU project by Torbjorn Granlund.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL WITH A MUTABLE INTERFACE.  IT IS
   ONLY SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS
   ALMOST GUARANTEED THAT THEY WILL CHANGE OR DISAPPEAR IN A FUTURE GMP
   RELEASE.

Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

This file is part of the GNU MP Library.

The GNU MP Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the GNU MP Library.  If not, see http://www.gnu.org/licenses/.  */

#include "gmp.h"
#include "gmp-impl.h"


/*
  r[k+1] = r[k] - r[k] * (u*r[k] - 1)
  r[k+1] = r[k] + r[k] - r[k]*(u*r[k])
*/

/* This is intended for constant THRESHOLDs only, where the compiler can
   completely fold the result.  */
#define LOG2C(n) \
 (((n) >=    0x1) + ((n) >=    0x2) + ((n) >=    0x4) + ((n) >=    0x8) + \
  ((n) >=   0x10) + ((n) >=   0x20) + ((n) >=   0x40) + ((n) >=   0x80) + \
  ((n) >=  0x100) + ((n) >=  0x200) + ((n) >=  0x400) + ((n) >=  0x800) + \
  ((n) >= 0x1000) + ((n) >= 0x2000) + ((n) >= 0x4000) + ((n) >= 0x8000))

#if TUNE_PROGRAM_BUILD
#define NPOWS \
 ((sizeof(mp_size_t) > 6 ? 48 : 8*sizeof(mp_size_t)))
#else
#define NPOWS \
 ((sizeof(mp_size_t) > 6 ? 48 : 8*sizeof(mp_size_t)) - LOG2C (BINV_NEWTON_THRESHOLD))
#endif

mp_size_t
mpn_binvert_itch (mp_size_t n)
{
#if WANT_FFT
  if (ABOVE_THRESHOLD (n, 2 * MUL_FFT_MODF_THRESHOLD))
    return mpn_fft_next_size (n, mpn_fft_best_k (n, 0));
  else
#endif
    return 3 * (n - (n >> 1));
}

void
mpn_binvert (mp_ptr rp, mp_srcptr up, mp_size_t n, mp_ptr scratch)
{
  mp_ptr xp;
  mp_size_t rn, newrn;
  mp_size_t sizes[NPOWS], *sizp;
  mp_limb_t di;

  /* Compute the computation precisions from highest to lowest, leaving the
     base case size in 'rn'.  */
  sizp = sizes;
  for (rn = n; ABOVE_THRESHOLD (rn, BINV_NEWTON_THRESHOLD); rn = (rn + 1) >> 1)
    *sizp++ = rn;

  xp = scratch;

  /* Compute a base value using a low-overhead O(n^2) algorithm.  FIXME: We
     should call some divide-and-conquer lsb division function here for an
     operand subrange.  */
  MPN_ZERO (xp, rn);
  xp[0] = 1;
  binvert_limb (di, up[0]);
  if (BELOW_THRESHOLD (rn, DC_BDIV_Q_THRESHOLD))
    mpn_sb_bdiv_q (rp, xp, rn, up, rn, -di);
  else
    mpn_dc_bdiv_q (rp, xp, rn, up, rn, -di);

  /* Use Newton iterations to get the desired precision.  */
  for (; rn < n; rn = newrn)
    {
      newrn = *--sizp;

#if WANT_FFT
      if (ABOVE_THRESHOLD (newrn, 2 * MUL_FFT_MODF_THRESHOLD))
	{
	  int k;
	  mp_size_t m, i;

	  k = mpn_fft_best_k (newrn, 0);
	  m = mpn_fft_next_size (newrn, k);
	  mpn_mul_fft (xp, m, up, newrn, rp, rn, k);
	  for (i = rn - 1; i >= 0; i--)
	    if (xp[i] > (i == 0))
	      {
		mpn_add_1 (xp + rn, xp + rn, newrn - rn, 1);
		break;
	      }
	}
      else
#endif
	mpn_mul (xp, up, newrn, rp, rn);
      mpn_mullow_n (rp + rn, rp, xp + rn, newrn - rn);
      mpn_neg_n (rp + rn, rp + rn, newrn - rn);
    }
}
