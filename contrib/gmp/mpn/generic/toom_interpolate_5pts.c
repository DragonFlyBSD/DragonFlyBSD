/* mpn_toom_interpolate_5pts -- Interpolate for toom3, 33, 42.

   Contributed to the GNU project by Robert Harley.
   Improvements by Paul Zimmermann and Marco Bodrato.

   THE FUNCTION IN THIS FILE IS INTERNAL WITH A MUTABLE INTERFACE.  IT IS ONLY
   SAFE TO REACH IT THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS ALMOST
   GUARANTEED THAT IT WILL CHANGE OR DISAPPEAR IN A FUTURE GNU MP RELEASE.

Copyright 2000, 2001, 2002, 2003, 2005, 2006, 2007 Free Software Foundation,
Inc.

This file is part of the GNU MP Library.

The GNU MP Library is free software; you can redistribute it and/or modify it
under the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
for more details.

You should have received a copy of the GNU Lesser General Public License
along with the GNU MP Library.  If not, see http://www.gnu.org/licenses/.  */

#include "gmp.h"
#include "gmp-impl.h"

void
mpn_toom_interpolate_5pts (mp_ptr c, mp_ptr v2, mp_ptr vm1,
			   mp_size_t k, mp_size_t twor, int sa,
			   mp_limb_t vinf0, mp_ptr ws)
{
  mp_limb_t cy, saved;
  mp_size_t twok = k + k;
  mp_size_t kk1 = twok + 1;
  mp_ptr c1, v1, c3, vinf, c5;
  mp_limb_t cout; /* final carry, should be zero at the end */

  c1 = c  + k;
  v1 = c1 + k;
  c3 = v1 + k;
  vinf = c3 + k;
  c5 = vinf + k;

#define v0 (c)
  /* (1) v2 <- v2-vm1 < v2+|vm1|,       (16 8 4 2 1) - (1 -1 1 -1  1) =
     thus 0 <= v2 < 50*B^(2k) < 2^6*B^(2k)             (15 9 3  3  0)
  */
  if (sa <= 0)
    mpn_add_n (v2, v2, vm1, kk1);
  else
    mpn_sub_n (v2, v2, vm1, kk1);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       v1       hi(vinf)       |vm1|     v2-vm1      EMPTY */

  ASSERT_NOCARRY (mpn_divexact_by3 (v2, v2, kk1));    /* v2 <- v2 / 3 */
						      /* (5 3 1 1 0)*/

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       v1      hi(vinf)       |vm1|     (v2-vm1)/3    EMPTY */

  /* (2) vm1 <- tm1 := (v1 - sa*vm1) / 2  [(1 1 1 1 1) - (1 -1 1 -1 1)] / 2 =
     tm1 >= 0                                            (0  1 0  1 0)
     No carry comes out from {v1, kk1} +/- {vm1, kk1},
     and the division by two is exact */
  if (sa <= 0)
    {
#ifdef HAVE_NATIVE_mpn_rsh1add_n
      mpn_rsh1add_n (vm1, v1, vm1, kk1);
#else
      mpn_add_n (vm1, v1, vm1, kk1);
      mpn_rshift (vm1, vm1, kk1, 1);
#endif
    }
  else
    {
#ifdef HAVE_NATIVE_mpn_rsh1sub_n
      mpn_rsh1sub_n (vm1, v1, vm1, kk1);
#else
      mpn_sub_n (vm1, v1, vm1, kk1);
      mpn_rshift (vm1, vm1, kk1, 1);
#endif
    }

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       v1        hi(vinf)       tm1     (v2-vm1)/3    EMPTY */

  /* (3) v1 <- t1 := v1 - v0    (1 1 1 1 1) - (0 0 0 0 1) = (1 1 1 1 0)
     t1 >= 0
  */
  vinf[0] -= mpn_sub_n (v1, v1, c, twok);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0     v1-v0        hi(vinf)       tm1     (v2-vm1)/3    EMPTY */

  /* (4) v2 <- t2 := ((v2-vm1)/3-t1)/2 = (v2-vm1-3*t1)/6
     t2 >= 0                  [(5 3 1 1 0) - (1 1 1 1 0)]/2 = (2 1 0 0 0)
  */
#ifdef HAVE_NATIVE_mpn_rsh1sub_n
  mpn_rsh1sub_n (v2, v2, v1, kk1);
#else
  mpn_sub_n (v2, v2, v1, kk1);
  mpn_rshift (v2, v2, kk1, 1);
#endif

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0     v1-v0        hi(vinf)     tm1    (v2-vm1-3t1)/6    EMPTY */

  /* (5) v1 <- t1-tm1           (1 1 1 1 0) - (0 1 0 1 0) = (1 0 1 0 0)
     result is v1 >= 0
  */
  mpn_sub_n (v1, v1, vm1, kk1);

  /* {c,2k} {c+2k,2k+1} {c+4k+1,2r-1} {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0   v1-v0-tm1      hi(vinf)     tm1    (v2-vm1-3t1)/6    EMPTY */

  /* (6) v2 <- v2 - 2*vinf,     (2 1 0 0 0) - 2*(1 0 0 0 0) = (0 1 0 0 0)
     result is v2 >= 0 */
  saved = vinf[0];       /* Remember v1's highest byte (will be overwritten). */
  vinf[0] = vinf0;       /* Set the right value for vinf0                     */
#ifdef HAVE_NATIVE_mpn_sublsh1_n
  cy = mpn_sublsh1_n (v2, v2, vinf, twor);
#else
  cy = mpn_lshift (ws, vinf, twor, 1);
  cy += mpn_sub_n (v2, v2, ws, twor);
#endif
  MPN_DECR_U (v2 + twor, kk1 - twor, cy);

  /* (7) v1 <- v1 - vinf,       (1 0 1 0 0) - (1 0 0 0 0) = (0 0 1 0 0)
     result is >= 0 */
  cy = mpn_sub_n (v1, v1, vinf, twor);          /* vinf is at most twor long.  */
  vinf[0] = saved;
  MPN_DECR_U (v1 + twor, kk1 - twor, cy);       /* Treat the last bytes.       */
  __GMPN_ADD_1 (cout, vinf, vinf, twor, vinf0); /* Add vinf0, propagate carry. */

  /* (8) vm1 <- vm1-t2          (0 1 0 1 0) - (0 1 0 0 0) = (0 0 0 1 0)
     vm1 >= 0
  */
  mpn_sub_n (vm1, vm1, v2, kk1);            /* No overlapping here.        */

  /********************* Beginning the final phase **********************/

  /* {c,2k} {c+2k,2k  } {c+4k ,2r } {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       t1      hi(t1)+vinf   tm1    (v2-vm1-3t1)/6    EMPTY */

  /* (9) add t2 in {c+3k, ...} */
  cy = mpn_add_n (c3, c3, v2, kk1);
  __GMPN_ADD_1 (cout, c5 + 1, c5 + 1, twor - k - 1, cy); /* 2n-(5k+1) = 2r-k-1 */

  /* {c,2k} {c+2k,2k  } {c+4k ,2r } {t,2k+1} {t+2k+1,2k+1} {t+4k+2,2r}
       v0       t1      hi(t1)+vinf   tm1    (v2-vm1-3t1)/6    EMPTY */
  /* c   c+k  c+2k  c+3k  c+4k      t   t+2k+1  t+4k+2
     v0       t1         vinf      tm1  t2
		    +t2 */

  /* add vm1 in {c+k, ...} */
  cy = mpn_add_n (c1, c1, vm1, kk1);
  __GMPN_ADD_1 (cout, c3 + 1, c3 + 1, twor + k - 1, cy); /* 2n-(3k+1) = 2r+k-1 */

  /* c   c+k  c+2k  c+3k  c+4k      t   t+2k+1  t+4k+2
     v0       t1         vinf      tm1  t2
	  +tm1      +t2    */

#undef v0
#undef t2
}
