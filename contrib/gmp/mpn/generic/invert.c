/* Compute {up,n}^(-1).

Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include "gmp.h"
#include "gmp-impl.h"

/* Formulas:
	z = 2z-(zz)d
	z = 2z-(zd)z
	z = z(2-zd)
	z = z-z*(zd-1)
	z = z+z*(1-zd)
*/

mp_size_t
mpn_invert_itch (mp_size_t n)
{
  return 3 * n + 2;
}

void
mpn_invert (mp_ptr ip, mp_srcptr dp, mp_size_t n, mp_ptr scratch)
{
  mp_ptr np, rp;
  mp_size_t i;
  TMP_DECL;

  TMP_MARK;
  if (scratch == NULL)
    {
      scratch = TMP_ALLOC_LIMBS (mpn_invert_itch (n));
    }

  np = scratch;					/* 2 * n limbs */
  rp = scratch + 2 * n;				/* n + 2 limbs */
  for (i = n - 1; i >= 0; i--)
    np[i] = ~CNST_LIMB(0);
  mpn_com_n (np + n, dp, n);
  mpn_tdiv_qr (rp, ip, 0L, np, 2 * n, dp, n);
  MPN_COPY (ip, rp, n);

  TMP_FREE;
}
