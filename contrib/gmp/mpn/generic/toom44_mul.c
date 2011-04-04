/* mpn_toom44_mul -- Multiply {ap,an} and {bp,bn} where an and bn are close in
   size.  Or more accurately, bn <= an < (4/3)bn.

   Contributed to the GNU project by Torbjorn Granlund and Marco Bodrato.

   THE FUNCTION IN THIS FILE IS INTERNAL WITH A MUTABLE INTERFACE.  IT IS ONLY
   SAFE TO REACH IT THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS ALMOST
   GUARANTEED THAT IT WILL CHANGE OR DISAPPEAR IN A FUTURE GNU MP RELEASE.

Copyright 2006, 2007, 2008 Free Software Foundation, Inc.

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


/*
  Things to work on:

  1. Trim allocation.  The allocations for as1, asm1, bs1, and bsm1 could be
     avoided by instead reusing the pp area and the scratch area.
  2. Use new toom functions for the recursive calls.
*/

#include "gmp.h"
#include "gmp-impl.h"

/* Evaluate in: -1, -1/2, 0, +1/2, +1, +2, +inf

  <-s--><--n--><--n--><--n-->
   ____ ______ ______ ______
  |_a3_|___a2_|___a1_|___a0_|
   |b3_|___b2_|___b1_|___b0_|
   <-t-><--n--><--n--><--n-->

  v0  =   a0             *  b0              #    A(0)*B(0)
  v1  = ( a0+ a1+ a2+ a3)*( b0+ b1+ b2+ b3) #    A(1)*B(1)      ah  <= 3   bh  <= 3
  vm1 = ( a0- a1+ a2- a3)*( b0- b1+ b2- b3) #   A(-1)*B(-1)    |ah| <= 1  |bh| <= 1
  v2  = ( a0+2a1+4a2+8a3)*( b0+2b1+4b2+8b3) #    A(2)*B(2)      ah  <= 14  bh  <= 14
  vh  = (8a0+4a1+2a2+ a3)*(8b0+4b1+2b2+ b3) #  A(1/2)*B(1/2)    ah  <= 14  bh  <= 14
  vmh = (8a0-4a1+2a2- a3)*(8b0-4b1+2b2- b3) # A(-1/2)*B(-1/2)  -4<=ah<=9  -4<=bh<=9
  vinf=               a3 *          b2      #  A(inf)*B(inf)
*/

#if TUNE_PROGRAM_BUILD
#define MAYBE_mul_basecase 1
#define MAYBE_mul_toom22   1
#define MAYBE_mul_toom44   1
#else
#define MAYBE_mul_basecase						\
  (MUL_TOOM44_THRESHOLD < 4 * MUL_KARATSUBA_THRESHOLD)
#define MAYBE_mul_toom22						\
  (MUL_TOOM44_THRESHOLD < 4 * MUL_TOOM33_THRESHOLD)
#define MAYBE_mul_toom44						\
  (MUL_FFT_THRESHOLD >= 4 * MUL_TOOM44_THRESHOLD)
#endif

#define TOOM44_MUL_N_REC(p, a, b, n, ws)				\
  do {									\
    if (MAYBE_mul_basecase						\
	&& BELOW_THRESHOLD (n, MUL_KARATSUBA_THRESHOLD))		\
      mpn_mul_basecase (p, a, n, b, n);					\
    else if (MAYBE_mul_toom22						\
	     && BELOW_THRESHOLD (n, MUL_TOOM33_THRESHOLD))		\
      mpn_kara_mul_n (p, a, b, n, ws);					\
    else if (! MAYBE_mul_toom44						\
	     || BELOW_THRESHOLD (n, MUL_TOOM44_THRESHOLD))		\
      mpn_toom3_mul_n (p, a, b, n, ws);					\
    else								\
      mpn_toom44_mul (p, a, n, b, n, ws);				\
  } while (0)

void
mpn_toom44_mul (mp_ptr pp,
		mp_srcptr ap, mp_size_t an,
		mp_srcptr bp, mp_size_t bn,
		mp_ptr scratch)
{
  mp_size_t n, s, t;
  mp_limb_t cy;
  mp_ptr gp, hp;
  mp_ptr as1, asm1, as2, ash, asmh;
  mp_ptr bs1, bsm1, bs2, bsh, bsmh;
  enum toom4_flags flags;
  TMP_DECL;

#define a0  ap
#define a1  (ap + n)
#define a2  (ap + 2*n)
#define a3  (ap + 3*n)
#define b0  bp
#define b1  (bp + n)
#define b2  (bp + 2*n)
#define b3  (bp + 3*n)

  n = (an + 3) >> 2;

  s = an - 3 * n;
  t = bn - 3 * n;

  ASSERT (an >= bn);

  ASSERT (0 < s && s <= n);
  ASSERT (0 < t && t <= n);

  TMP_MARK;

  as1  = TMP_ALLOC_LIMBS (10 * n + 10);
  asm1 = as1  + n + 1;
  as2  = asm1 + n + 1;
  ash  = as2  + n + 1;
  asmh = ash  + n + 1;
  bs1  = asmh + n + 1;
  bsm1 = bs1  + n + 1;
  bs2  = bsm1 + n + 1;
  bsh  = bs2  + n + 1;
  bsmh = bsh  + n + 1;

  gp = pp;
  hp = pp + n + 1;

  flags = 0;

  /* Compute as1 and asm1.  */
  gp[n]  = mpn_add_n (gp, a0, a2, n);
  hp[n]  = mpn_add (hp, a1, n, a3, s);
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_addsub_n (as1, asm1, hp, gp, n + 1);
      flags ^= toom4_w3_neg;
    }
  else
    {
      mpn_addsub_n (as1, asm1, gp, hp, n + 1);
    }
#else
  mpn_add_n (as1, gp, hp, n + 1);
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_sub_n (asm1, hp, gp, n + 1);
      flags ^= toom4_w3_neg;
    }
  else
    {
      mpn_sub_n (asm1, gp, hp, n + 1);
    }
#endif

  /* Compute as2.  */
#if HAVE_NATIVE_mpn_addlsh1_n
  cy  = mpn_addlsh1_n (as2, a2, a3, s);
  if (s != n)
    cy = mpn_add_1 (as2 + s, a2 + s, n - s, cy);
  cy = 2 * cy + mpn_addlsh1_n (as2, a1, as2, n);
  cy = 2 * cy + mpn_addlsh1_n (as2, a0, as2, n);
#else
  cy  = mpn_lshift (as2, a3, s, 1);
  cy += mpn_add_n (as2, a2, as2, s);
  if (s != n)
    cy = mpn_add_1 (as2 + s, a2 + s, n - s, cy);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a1, as2, n);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a0, as2, n);
#endif
  as2[n] = cy;

  /* Compute ash and asmh.  */
  cy  = mpn_lshift (gp, a0, n, 3);			/*  8a0             */
#if HAVE_NATIVE_mpn_addlsh1_n
  gp[n] = cy + mpn_addlsh1_n (gp, gp, a2, n);		/*  8a0 + 2a2       */
#else
  cy += mpn_lshift (hp, a2, n, 1);			/*        2a2       */
  gp[n] = cy + mpn_add_n (gp, gp, hp, n);		/*  8a0 + 2a2       */
#endif
  cy = mpn_lshift (hp, a1, n, 2);			/*  4a1             */
  hp[n] = cy + mpn_add (hp, hp, n, a3, s);		/*  4a1 +  a3       */
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_addsub_n (ash, asmh, hp, gp, n + 1);
      flags ^= toom4_w1_neg;
    }
  else
    {
      mpn_addsub_n (ash, asmh, gp, hp, n + 1);
    }
#else
  mpn_add_n (ash, gp, hp, n + 1);
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_sub_n (asmh, hp, gp, n + 1);
      flags ^= toom4_w1_neg;
    }
  else
    {
      mpn_sub_n (asmh, gp, hp, n + 1);
    }
#endif

  /* Compute bs1 and bsm1.  */
  gp[n]  = mpn_add_n (gp, b0, b2, n);
  hp[n]  = mpn_add (hp, b1, n, b3, t);
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_addsub_n (bs1, bsm1, hp, gp, n + 1);
      flags ^= toom4_w3_neg;
    }
  else
    {
      mpn_addsub_n (bs1, bsm1, gp, hp, n + 1);
    }
#else
  mpn_add_n (bs1, gp, hp, n + 1);
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_sub_n (bsm1, hp, gp, n + 1);
      flags ^= toom4_w3_neg;
    }
  else
    {
      mpn_sub_n (bsm1, gp, hp, n + 1);
    }
#endif

  /* Compute bs2.  */
#if HAVE_NATIVE_mpn_addlsh1_n
  cy  = mpn_addlsh1_n (bs2, b2, b3, t);
  if (t != n)
    cy = mpn_add_1 (bs2 + t, b2 + t, n - t, cy);
  cy = 2 * cy + mpn_addlsh1_n (bs2, b1, bs2, n);
  cy = 2 * cy + mpn_addlsh1_n (bs2, b0, bs2, n);
#else
  cy  = mpn_lshift (bs2, b3, t, 1);
  cy += mpn_add_n (bs2, b2, bs2, t);
  if (t != n)
    cy = mpn_add_1 (bs2 + t, b2 + t, n - t, cy);
  cy = 2 * cy + mpn_lshift (bs2, bs2, n, 1);
  cy += mpn_add_n (bs2, b1, bs2, n);
  cy = 2 * cy + mpn_lshift (bs2, bs2, n, 1);
  cy += mpn_add_n (bs2, b0, bs2, n);
#endif
  bs2[n] = cy;

  /* Compute bsh and bsmh.  */
  cy  = mpn_lshift (gp, b0, n, 3);			/*  8b0             */
#if HAVE_NATIVE_mpn_addlsh1_n
  gp[n] = cy + mpn_addlsh1_n (gp, gp, b2, n);		/*  8b0 + 2b2       */
#else
  cy += mpn_lshift (hp, b2, n, 1);			/*        2b2       */
  gp[n] = cy + mpn_add_n (gp, gp, hp, n);		/*  8b0 + 2b2       */
#endif
  cy = mpn_lshift (hp, b1, n, 2);			/*  4b1             */
  hp[n] = cy + mpn_add (hp, hp, n, b3, t);		/*  4b1 +  b3       */
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_addsub_n (bsh, bsmh, hp, gp, n + 1);
      flags ^= toom4_w1_neg;
    }
  else
    {
      mpn_addsub_n (bsh, bsmh, gp, hp, n + 1);
    }
#else
  mpn_add_n (bsh, gp, hp, n + 1);
  if (mpn_cmp (gp, hp, n + 1) < 0)
    {
      mpn_sub_n (bsmh, hp, gp, n + 1);
      flags ^= toom4_w1_neg;
    }
  else
    {
      mpn_sub_n (bsmh, gp, hp, n + 1);
    }
#endif

  ASSERT (as1[n] <= 3);
  ASSERT (bs1[n] <= 3);
  ASSERT (asm1[n] <= 1);
  ASSERT (bsm1[n] <= 1);
  ASSERT (as2[n] <= 14);
  ASSERT (bs2[n] <= 14);
  ASSERT (ash[n] <= 14);
  ASSERT (bsh[n] <= 14);
  ASSERT (asmh[n] <= 9);
  ASSERT (bsmh[n] <= 9);

#define v0    pp				/* 2n */
#define v1    (scratch + 6 * n + 6)		/* 2n+1 */
#define vm1   scratch				/* 2n+1 */
#define v2    (scratch + 2 * n + 2)		/* 2n+1 */
#define vinf  (pp + 6 * n)			/* s+t */
#define vh    (pp + 2 * n)			/* 2n+1 */
#define vmh   (scratch + 4 * n + 4)
#define scratch_out  (scratch + 8 * n + 8)

  /* vm1, 2n+1 limbs */
  TOOM44_MUL_N_REC (vm1, asm1, bsm1, n + 1, scratch_out);	/* vm1, 2n+1 limbs */

  TOOM44_MUL_N_REC (v2 , as2 , bs2 , n + 1, scratch_out);	/* v2,  2n+1 limbs */

  if (s > t)  mpn_mul (vinf, a3, s, b3, t);
  else   TOOM44_MUL_N_REC (vinf, a3, b3, s, scratch_out);	/* vinf, s+t limbs */

  TOOM44_MUL_N_REC (v1 , as1 , bs1 , n + 1, scratch_out);	/* v1,  2n+1 limbs */

  TOOM44_MUL_N_REC (vh , ash , bsh , n + 1, scratch_out);

  TOOM44_MUL_N_REC (vmh, asmh, bsmh, n + 1, scratch_out);

  TOOM44_MUL_N_REC (v0 , ap  , bp  , n    , scratch_out);	/* v0,  2n limbs */

  mpn_toom_interpolate_7pts (pp, n, flags, vmh, vm1, v1, v2, s + t, scratch_out);

  TMP_FREE;
}
