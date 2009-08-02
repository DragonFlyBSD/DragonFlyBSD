/* mpn_mu_div_q, mpn_preinv_mu_div_q.

   Contributed to the GNU project by Torbjörn Granlund.

   THE FUNCTIONS IN THIS FILE ARE INTERNAL WITH A MUTABLE INTERFACE.  IT IS
   ONLY SAFE TO REACH THEM THROUGH DOCUMENTED INTERFACES.  IN FACT, IT IS
   ALMOST GUARANTEED THAT THEY WILL CHANGE OR DISAPPEAR IN A FUTURE GMP
   RELEASE.

Copyright 2005, 2006, 2007 Free Software Foundation, Inc.

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

  1. This is a rudimentary implementation of mpn_mu_div_q.  The algorithm is
     probably close to optimal, except when mpn_mu_divappr_q fails.

     An alternative which could be considered for much simpler code for the
     complex qn>=dn arm would be to allocate a temporary nn+1 limb buffer, then
     simply call mpn_mu_divappr_q.  Such a temporary allocation is
     unfortunately very large.

  2. Instead of falling back to mpn_mu_div_qr when we detect a possible
     mpn_mu_divappr_q rounding problem, we could multiply and compare.
     Unfortunately, since mpn_mu_divappr_q does not return the partial
     remainder, this also doesn't become optimal.  A mpn_mu_divappr_qr
     could solve that.

  3. The allocations done here should be made from the scratch area.
*/

#include <stdlib.h>		/* for NULL */
#include "gmp.h"
#include "gmp-impl.h"


mp_limb_t
mpn_mu_div_q (mp_ptr qp,
	      mp_ptr np, mp_size_t nn,
	      mp_srcptr dp, mp_size_t dn,
	      mp_ptr scratch)
{
  mp_ptr tp, rp, ip, this_ip;
  mp_size_t qn, in, this_in;
  mp_limb_t cy;
  TMP_DECL;

  TMP_MARK;

  qn = nn - dn;

  tp = TMP_BALLOC_LIMBS (qn + 1);

  if (qn >= dn)			/* nn >= 2*dn + 1 */
    {
      /* Find max inverse size needed by the two preinv calls.  */
      if (dn != qn)
	{
	  mp_size_t in1, in2;

	  in1 = mpn_mu_div_qr_choose_in (qn - dn, dn, 0);
	  in2 = mpn_mu_divappr_q_choose_in (dn + 1, dn, 0);
	  in = MAX (in1, in2);
	}
      else
	{
	  in = mpn_mu_divappr_q_choose_in (dn + 1, dn, 0);
	}

      ip = TMP_BALLOC_LIMBS (in + 1);

      if (dn == in)
	{
	  MPN_COPY (scratch + 1, dp, in);
	  scratch[0] = 1;
	  mpn_invert (ip, scratch, in + 1, NULL);
	  MPN_COPY_INCR (ip, ip + 1, in);
	}
      else
	{
	  cy = mpn_add_1 (scratch, dp + dn - (in + 1), in + 1, 1);
	  if (UNLIKELY (cy != 0))
	    MPN_ZERO (ip, in);
	  else
	    {
	      mpn_invert (ip, scratch, in + 1, NULL);
	      MPN_COPY_INCR (ip, ip + 1, in);
	    }
	}

       /* |_______________________|   dividend
			 |________|   divisor  */
      rp = TMP_BALLOC_LIMBS (2 * dn + 1);
      if (dn != qn)		/* FIXME: perhaps mpn_mu_div_qr should DTRT */
	{
	  this_in = mpn_mu_div_qr_choose_in (qn - dn, dn, 0);
	  this_ip = ip + in - this_in;
	  mpn_preinv_mu_div_qr (tp + dn + 1, rp + dn + 1, np + dn, qn, dp, dn,
				this_ip, this_in, scratch);
	}
      else
	MPN_COPY (rp + dn + 1, np + dn, dn);

      MPN_COPY (rp + 1, np, dn);
      rp[0] = 0;
      this_in = mpn_mu_divappr_q_choose_in (dn + 1, dn, 0);
      this_ip = ip + in - this_in;
      mpn_preinv_mu_divappr_q (tp, rp, 2*dn + 1, dp, dn, this_ip, this_in, scratch);

      /* The max error of mpn_mu_divappr_q is +4.  If the low quotient limb is
	 greater than the max error, we cannot trust the quotient.  */
      if (tp[0] > 4)
	{
	  MPN_COPY (qp, tp + 1, qn);
	}
      else
	{
	  /* Fall back to plain mpn_mu_div_qr.  */
	  mpn_mu_div_qr (qp, rp, np, nn, dp, dn, scratch);
	}
    }
  else
    {
       /* |_______________________|   dividend
		 |________________|   divisor  */
      mpn_mu_divappr_q (tp, np + nn - (2*qn + 2), 2*qn + 2, dp + dn - (qn + 1), qn + 1, scratch);

      if (tp[0] > 4)
	{
	  MPN_COPY (qp, tp + 1, qn);
	}
      else
	{
	  rp = TMP_BALLOC_LIMBS (dn);
	  mpn_mu_div_qr (qp, rp, np, nn, dp, dn, scratch);
	}
    }

  TMP_FREE;
  return 0;
}
