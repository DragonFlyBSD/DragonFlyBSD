/* mpn_dc_div_q -- divide-and-conquer division, returning exact quotient only.

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
mpn_dc_div_q (mp_ptr qp, mp_ptr np, mp_size_t nn, mp_srcptr dp, mp_size_t dn)
{
  mp_ptr tp, wp;
  mp_limb_t qh;
  mp_size_t qn;
  TMP_DECL;

  TMP_MARK;

  tp = TMP_SALLOC_LIMBS (nn + 1);
  MPN_COPY (tp + 1, np, nn);
  tp[0] = 0;

  qn = nn - dn;
  wp = TMP_SALLOC_LIMBS (qn + 1);

  qh = mpn_dc_divappr_q (wp, tp, nn + 1, dp, dn);

  if (wp[0] == 0)
    /* FIXME: Should multiply and subtract here, not recompute from scratch.  */
    qh = mpn_dc_div_qr (qp, np, nn, dp, dn);
  else
    MPN_COPY (qp, wp + 1, qn);

  return qh;
}
