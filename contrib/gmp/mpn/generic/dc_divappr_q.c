/* mpn_dc_divappr_q -- divide-and-conquer division, returning only approximate
   quotient.  The quotient retuened is either correct, or unity too large.

   Contributed to the GNU project by Torbjörn Granlund.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL WITH A MUTABLE INTERFACE.  IT IS
   ONLY SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS
   ALMOST GUARANTEED THAT THEY WILL CHANGE OR DISAPPEAR IN A FUTURE GMP
   RELEASE.

Copyright 2006, 2007 Free Software Foundation, Inc.

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


mp_limb_t
mpn_dc_divappr_q_n (mp_ptr qp, mp_ptr np, mp_srcptr dp, mp_size_t n,
		    mp_srcptr dip, mp_ptr tp)
{
  mp_size_t lo, hi;
  mp_limb_t cy, qh, ql;

  lo = n >> 1;			/* floor(n/2) */
  hi = n - lo;			/* ceil(n/2) */

  if (BELOW_THRESHOLD (hi, DC_DIV_QR_THRESHOLD))
    qh = mpn_sb_div_qr (qp + lo, np + 2 * lo, 2 * hi, dp + lo, hi, dip);
  else
    qh = mpn_dc_div_qr_n (qp + lo, np + 2 * lo, dp + lo, hi, dip, tp);

  mpn_mul (tp, qp + lo, hi, dp, lo);

  cy = mpn_sub_n (np + lo, np + lo, tp, n);
  if (qh != 0)
    cy += mpn_sub_n (np + n, np + n, dp, lo);

  while (cy != 0)
    {
      qh -= mpn_sub_1 (qp + lo, qp + lo, hi, 1);
      cy -= mpn_add_n (np + lo, np + lo, dp, n);
    }

  if (BELOW_THRESHOLD (lo, DC_DIVAPPR_Q_THRESHOLD))
    ql = mpn_sb_divappr_q (qp, np + hi, 2 * lo, dp + hi, lo, dip);
  else
    ql = mpn_dc_divappr_q_n (qp, np + hi, dp + hi, lo, dip, tp);

  if (UNLIKELY (ql != 0))
    {
      mp_size_t i;
      for (i = 0; i < lo; i++)
	qp[i] = GMP_NUMB_MASK;
    }

  return qh;
}

mp_limb_t
mpn_preinv_dc_divappr_q (mp_ptr qp,
			 mp_ptr np, mp_size_t nn,
			 mp_srcptr dp, mp_size_t dn,
			 mp_srcptr dip)
{
  mp_size_t qn;
  mp_limb_t qh, cy, qsave;
  mp_ptr tp;
  TMP_DECL;

  TMP_MARK;

  tp = TMP_SALLOC_LIMBS (dn+1);

  qn = nn - dn;
  qp += qn;
  np += nn;
  dp += dn;

  if (qn > dn)
    {
      qn++;			/* pretend we'll need an extra limb */
      /* Reduce qn mod dn without division, optimizing small operations.  */
      do
	qn -= dn;
      while (qn > dn);

      qp -= qn;			/* point at low limb of next quotient block */
      np -= qn;			/* point in the middle of partial remainder */

      /* Perform the typically smaller block first.  */
      if (BELOW_THRESHOLD (qn, DC_DIV_QR_THRESHOLD))
	qh = mpn_sb_div_qr (qp, np - qn, 2 * qn, dp - qn, qn, dip);
      else
	qh = mpn_dc_div_qr_n (qp, np - qn, dp - qn, qn, dip, tp);

      if (qn != dn)
	{
	  if (qn > dn - qn)
	    mpn_mul (tp, qp, qn, dp - dn, dn - qn);
	  else
	    mpn_mul (tp, dp - dn, dn - qn, qp, qn);

	  cy = mpn_sub_n (np - dn, np - dn, tp, dn);
	  if (qh != 0)
	    cy += mpn_sub_n (np - dn + qn, np - dn + qn, dp - dn, dn - qn);

	  while (cy != 0)
	    {
	      qh -= mpn_sub_1 (qp, qp, qn, 1);
	      cy -= mpn_add_n (np - dn, np - dn, dp - dn, dn);
	    }
	}

      qn = nn - dn - qn + 1;
      while (qn > dn)
	{
	  qp -= dn;
	  np -= dn;
	  mpn_dc_div_qr_n (qp, np - dn, dp - dn, dn, dip, tp);
	  qn -= dn;
	}

      /* Since we pretended we'd need an extra quotient limb before, we now
	 have made sure the code above left just dn-1=qn quotient limbs to
	 develop.  Develop that plus a guard limb. */
      qn--;
      qp -= qn;
      np -= dn;
      qsave = qp[qn];
      mpn_dc_divappr_q_n (qp, np - dn, dp - dn, dn, dip, tp);
      MPN_COPY_INCR (qp, qp + 1, qn);
      qp[qn] = qsave;
    }
  else
    {
      if (qn == 0)
	{
	  qh = mpn_cmp (np - dn, dp - dn, dn) >= 0;
	  if (qh)
	    mpn_sub_n (np - dn, np - dn, dp - dn, dn);
	  TMP_FREE;
	  return qh;
	}

      qp -= qn;			/* point at low limb of next quotient block */
      np -= qn;			/* point in the middle of partial remainder */

      if (BELOW_THRESHOLD (qn, DC_DIVAPPR_Q_THRESHOLD))
	 /* Full precision.  Optimal?  */
	qh = mpn_sb_divappr_q (qp, np - dn, nn, dp - dn, dn, dip);
      else
	{
	  /* Put quotient in tp, use qp as temporary, since qp lacks a limb.  */
	  qh = mpn_dc_divappr_q_n (tp, np - qn - 2, dp - (qn + 1), qn + 1, dip, qp);
	  MPN_COPY (qp, tp + 1, qn);
	}
    }

  TMP_FREE;
  return qh;
}

mp_limb_t
mpn_dc_divappr_q (mp_ptr qp, mp_ptr np, mp_size_t nn, mp_srcptr dp, mp_size_t dn)
{
  mp_limb_t cy;
  mp_limb_t xp[2], dip[2];

  ASSERT (dn >= 2);

  cy = mpn_add_1 (xp, dp + dn - 2, 2, 1);
  if (cy != 0)
    dip[0] = dip[1] = 0;
  else
    {
      mp_limb_t scratch[10];	/* FIXME */
      mpn_invert (dip, xp, 2, scratch);
    }

  return mpn_preinv_dc_divappr_q (qp, np, nn, dp, dn, dip);
}
