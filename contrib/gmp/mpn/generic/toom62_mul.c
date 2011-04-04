/* mpn_toom62_mul -- Multiply {ap,an} and {bp,bn} where an is nominally 3 times
   as large as bn.  Or more accurately, (5/2)bn < an < 6bn.

   Contributed to the GNU project by Torbjorn Granlund and Marco Bodrato.

   The idea of applying toom to unbalanced multiplication is due to Marco
   Bodrato and Alberto Zanoni.

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
     avoided by instead reusing the pp area and the scratch allocation.
*/

#include "gmp.h"
#include "gmp-impl.h"


/* Evaluate in: -1, -1/2, 0, +1/2, +1, +2, +inf

  <-s-><--n--><--n--><--n-->
   ___ ______ ______ ______ ______ ______
  |a5_|___a4_|___a3_|___a2_|___a1_|___a0_|
			     |_b1_|___b0_|
			     <-t--><--n-->

  v0  =    a0                       *   b0      #    A(0)*B(0)
  v1  = (  a0+  a1+ a2+ a3+  a4+  a5)*( b0+ b1) #    A(1)*B(1)      ah  <= 5   bh <= 1
  vm1 = (  a0-  a1+ a2- a3+  a4-  a5)*( b0- b1) #   A(-1)*B(-1)    |ah| <= 2   bh  = 0
  v2  = (  a0+ 2a1+4a2+8a3+16a4+32a5)*( b0+2b1) #    A(2)*B(2)      ah  <= 62  bh <= 2
  vh  = (32a0+16a1+8a2+4a3+ 2a4+  a5)*(2b0+ b1) #  A(1/2)*B(1/2)    ah  <= 62  bh <= 2
  vmh = (32a0-16a1+8a2-4a3+ 2a4-  a5)*(2b0- b1) # A(-1/2)*B(-1/2)  -20<=ah<=41 0<=bh<=1
  vinf=                           a5 *      b1  #  A(inf)*B(inf)
*/

void
mpn_toom62_mul (mp_ptr pp,
		mp_srcptr ap, mp_size_t an,
		mp_srcptr bp, mp_size_t bn,
		mp_ptr scratch)
{
  mp_size_t n, s, t;
  int vm1_neg, vmh_neg, bsm_neg;
  mp_limb_t cy;
  mp_ptr a0_a2, a1_a3;
  mp_ptr as1, asm1, as2, ash, asmh;
  mp_ptr bs1, bsm1, bs2, bsh, bsmh;
  enum toom4_flags flags;
  TMP_DECL;

#define a0  ap
#define a1  (ap + n)
#define a2  (ap + 2*n)
#define a3  (ap + 3*n)
#define a4  (ap + 4*n)
#define a5  (ap + 5*n)
#define b0  bp
#define b1  (bp + n)

  n = 1 + (an >= 3 * bn ? (an - 1) / (unsigned long) 6 : (bn - 1) >> 1);

  s = an - 5 * n;
  t = bn - n;

  ASSERT (0 < s && s <= n);
  ASSERT (0 < t && t <= n);

  TMP_MARK;

  as1 = TMP_SALLOC_LIMBS (n + 1);
  asm1 = TMP_SALLOC_LIMBS (n + 1);
  as2 = TMP_SALLOC_LIMBS (n + 1);
  ash = TMP_SALLOC_LIMBS (n + 1);
  asmh = TMP_SALLOC_LIMBS (n + 1);

  bs1 = TMP_SALLOC_LIMBS (n + 1);
  bsm1 = TMP_SALLOC_LIMBS (n);
  bs2 = TMP_SALLOC_LIMBS (n + 1);
  bsh = TMP_SALLOC_LIMBS (n + 1);
  bsmh = TMP_SALLOC_LIMBS (n + 1);

  a0_a2 = pp;
  a1_a3 = pp + n + 1;

  /* Compute as1 and asm1.  */
  a0_a2[n]  = mpn_add_n (a0_a2, a0, a2, n);
  a0_a2[n] += mpn_add_n (a0_a2, a0_a2, a4, n);
  a1_a3[n]  = mpn_add_n (a1_a3, a1, a3, n);
  a1_a3[n] += mpn_add (a1_a3, a1_a3, n, a5, s);
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (a0_a2, a1_a3, n + 1) < 0)
    {
      mpn_addsub_n (as1, asm1, a1_a3, a0_a2, n + 1);
      vm1_neg = 1;
    }
  else
    {
      mpn_addsub_n (as1, asm1, a0_a2, a1_a3, n + 1);
      vm1_neg = 0;
    }
#else
  mpn_add_n (as1, a0_a2, a1_a3, n + 1);
  if (mpn_cmp (a0_a2, a1_a3, n + 1) < 0)
    {
      mpn_sub_n (asm1, a1_a3, a0_a2, n + 1);
      vm1_neg = 1;
    }
  else
    {
      mpn_sub_n (asm1, a0_a2, a1_a3, n + 1);
      vm1_neg = 0;
    }
#endif

  /* Compute as2.  */
#if HAVE_NATIVE_mpn_addlsh1_n
  cy  = mpn_addlsh1_n (as2, a4, a5, s);
  if (s != n)
    cy = mpn_add_1 (as2 + s, a4 + s, n - s, cy);
  cy = 2 * cy + mpn_addlsh1_n (as2, a3, as2, n);
  cy = 2 * cy + mpn_addlsh1_n (as2, a2, as2, n);
  cy = 2 * cy + mpn_addlsh1_n (as2, a1, as2, n);
  cy = 2 * cy + mpn_addlsh1_n (as2, a0, as2, n);
#else
  cy  = mpn_lshift (as2, a5, s, 1);
  cy += mpn_add_n (as2, a4, as2, s);
  if (s != n)
    cy = mpn_add_1 (as2 + s, a4 + s, n - s, cy);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a3, as2, n);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a2, as2, n);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a1, as2, n);
  cy = 2 * cy + mpn_lshift (as2, as2, n, 1);
  cy += mpn_add_n (as2, a0, as2, n);
#endif
  as2[n] = cy;

  /* Compute ash and asmh.  */
#if HAVE_NATIVE_mpn_addlsh_n
  cy  = mpn_addlsh_n (a0_a2, a2, a0, n, 2);		/* 4a0  +  a2       */
  cy = 4 * cy + mpn_addlsh_n (a0_a2, a4, a0_a2, n, 2);	/* 16a0 + 4a2 +  a4 */
  cy = 2 * cy + mpn_lshift (a0_a2, a0_a2, n, 1);	/* 32a0 + 8a2 + 2a4 */
  a0_a2[n] = cy;
  cy  = mpn_addlsh_n (a1_a3, a3, a1, n, 2);		/* 4a1              */
  cy = 4 * cy + mpn_addlsh_n (a1_a3, a5, a1_a3, n, 2);	/* 16a1 + 4a3       */
  a1_a3[n] = cy;
#else
  cy  = mpn_lshift (a0_a2, a0, n, 2);			/* 4a0              */
  cy += mpn_add_n (a0_a2, a2, a0_a2, n);		/* 4a0  +  a2       */
  cy = 4 * cy + mpn_lshift (a0_a2, a0_a2, n, 2);	/* 16a0 + 4a2       */
  cy += mpn_add_n (a0_a2, a4, a0_a2, n);		/* 16a0 + 4a2 +  a4 */
  cy = 2 * cy + mpn_lshift (a0_a2, a0_a2, n, 1);	/* 32a0 + 8a2 + 2a4 */
  a0_a2[n] = cy;
  cy  = mpn_lshift (a1_a3, a1, n, 2);			/* 4a1              */
  cy += mpn_add_n (a1_a3, a3, a1_a3, n);		/* 4a1  +  a3       */
  cy = 4 * cy + mpn_lshift (a1_a3, a1_a3, n, 2);	/* 16a1 + 4a3       */
  cy += mpn_add (a1_a3, a1_a3, n, a5, s);		/* 16a1 + 4a3 + a5  */
  a1_a3[n] = cy;
#endif
#if HAVE_NATIVE_mpn_addsub_n
  if (mpn_cmp (a0_a2, a1_a3, n + 1) < 0)
    {
      mpn_addsub_n (ash, asmh, a1_a3, a0_a2, n + 1);
      vmh_neg = 1;
    }
  else
    {
      mpn_addsub_n (ash, asmh, a0_a2, a1_a3, n + 1);
      vmh_neg = 0;
    }
#else
  mpn_add_n (ash, a0_a2, a1_a3, n + 1);
  if (mpn_cmp (a0_a2, a1_a3, n + 1) < 0)
    {
      mpn_sub_n (asmh, a1_a3, a0_a2, n + 1);
      vmh_neg = 1;
    }
  else
    {
      mpn_sub_n (asmh, a0_a2, a1_a3, n + 1);
      vmh_neg = 0;
    }
#endif

  /* Compute bs1 and bsm1.  */
  if (t == n)
    {
#if HAVE_NATIVE_mpn_addsub_n
      if (mpn_cmp (b0, b1, n) < 0)
	{
	  cy = mpn_addsub_n (bs1, bsm1, b1, b0, n);
	  bsm_neg = 1;
	}
      else
	{
	  cy = mpn_addsub_n (bs1, bsm1, b0, b1, n);
	  bsm_neg = 0;
	}
      bs1[n] = cy >> 1;
#else
      bs1[n] = mpn_add_n (bs1, b0, b1, n);
      if (mpn_cmp (b0, b1, n) < 0)
	{
	  mpn_sub_n (bsm1, b1, b0, n);
	  bsm_neg = 1;
	}
      else
	{
	  mpn_sub_n (bsm1, b0, b1, n);
	  bsm_neg = 0;
	}
#endif
    }
  else
    {
      bs1[n] = mpn_add (bs1, b0, n, b1, t);
      if (mpn_zero_p (b0 + t, n - t) && mpn_cmp (b0, b1, t) < 0)
	{
	  mpn_sub_n (bsm1, b1, b0, t);
	  MPN_ZERO (bsm1 + t, n - t);
	  bsm_neg = 1;
	}
      else
	{
	  mpn_sub (bsm1, b0, n, b1, t);
	  bsm_neg = 0;
	}
    }

  vm1_neg ^= bsm_neg;

  /* Compute bs2, recycling bs1. bs2=bs1+b1  */
  mpn_add (bs2, bs1, n + 1, b1, t);

  /* Compute bsh and bsmh, recycling bs1 and bsm1. bsh=bs1+b0; bsmh=bsmh+b0  */
  if (bsm_neg == 1)
    {
      bsmh[n] = 0;
      if (mpn_cmp (bsm1, b0, n) < 0)
	{
	  bsm_neg = 0;
	  mpn_sub_n (bsmh, b0, bsm1, n);
	}
      else
	mpn_sub_n (bsmh, bsm1, b0, n);
    }
  else
    bsmh[n] = mpn_add_n (bsmh, bsm1, b0, n);
  mpn_add (bsh, bs1, n + 1, b0, n);
  vmh_neg ^= bsm_neg;


  ASSERT (as1[n] <= 5);
  ASSERT (bs1[n] <= 1);
  ASSERT (asm1[n] <= 2);
/*ASSERT (bsm1[n] == 0);*/
  ASSERT (as2[n] <= 62);
  ASSERT (bs2[n] <= 2);
  ASSERT (ash[n] <= 62);
  ASSERT (bsh[n] <= 2);
  ASSERT (asmh[n] <= 41);
  ASSERT (bsmh[n] <= 1);

#define v0    pp				/* 2n */
#define v1    (scratch + 6 * n + 6)		/* 2n+1 */
#define vinf  (pp + 6 * n)			/* s+t */
#define vm1   scratch				/* 2n+1 */
#define v2    (scratch + 2 * n + 2)		/* 2n+1 */
#define vh    (pp + 2 * n)			/* 2n+1 */
#define vmh   (scratch + 4 * n + 4)

  /* vm1, 2n+1 limbs */
  mpn_mul_n (vm1, asm1, bsm1, n);
  cy = 0;
  if (asm1[n] == 1)
    {
      cy = mpn_add_n (vm1 + n, vm1 + n, bsm1, n);
    }
  else if (asm1[n] == 2)
    {
#if HAVE_NATIVE_mpn_addlsh1_n
      cy = mpn_addlsh1_n (vm1 + n, vm1 + n, bsm1, n);
#else
      cy = mpn_addmul_1 (vm1 + n, bsm1, n, CNST_LIMB(2));
#endif
    }
  vm1[2 * n] = cy;

  mpn_mul_n (v2, as2, bs2, n + 1);		/* v2, 2n+1 limbs */

  /* vinf, s+t limbs */
  if (s > t)  mpn_mul (vinf, a5, s, b1, t);
  else        mpn_mul (vinf, b1, t, a5, s);

  /* v1, 2n+1 limbs */
  mpn_mul_n (v1, as1, bs1, n);
  if (as1[n] == 1)
    {
      cy = bs1[n] + mpn_add_n (v1 + n, v1 + n, bs1, n);
    }
  else if (as1[n] == 2)
    {
#if HAVE_NATIVE_mpn_addlsh1_n
      cy = 2 * bs1[n] + mpn_addlsh1_n (v1 + n, v1 + n, bs1, n);
#else
      cy = 2 * bs1[n] + mpn_addmul_1 (v1 + n, bs1, n, CNST_LIMB(2));
#endif
    }
  else if (as1[n] != 0)
    {
      cy = as1[n] * bs1[n] + mpn_addmul_1 (v1 + n, bs1, n, as1[n]);
    }
  else
    cy = 0;
  if (bs1[n] != 0)
    cy += mpn_add_n (v1 + n, v1 + n, as1, n);
  v1[2 * n] = cy;

  mpn_mul_n (vh, ash, bsh, n + 1);

  mpn_mul_n (vmh, asmh, bsmh, n + 1);

  mpn_mul_n (v0, ap, bp, n);			/* v0, 2n limbs */

  flags =  vm1_neg ? toom4_w3_neg : 0;
  flags |= vmh_neg ? toom4_w1_neg : 0;

  mpn_toom_interpolate_7pts (pp, n, flags, vmh, vm1, v1, v2, s + t, scratch + 8 * n + 8);

  TMP_FREE;
}
