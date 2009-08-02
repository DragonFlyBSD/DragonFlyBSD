/* mpn_mu_bdiv_qr -- divide-and-conquer Hensel division using a variant of
   Barrett's algorithm, returning quotient and remainder.

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

#include "gmp.h"
#include "gmp-impl.h"


/* Computes Hensel binary division of {np, 2*n} by {dp, n}.

   Output:

      q = n * d^{-1} mod 2^{qn * GMP_NUMB_BITS},

      r = (n - q * d) * 2^{-qn * GMP_NUMB_BITS}

   Stores q at qp. Stores the n least significant limbs of r at the high half
   of np, and returns the borrow from the subtraction n - q*d.

   d must be odd. dinv is (-d)^-1 mod 2^GMP_NUMB_BITS. */

void
mpn_mu_bdiv_qr (mp_ptr qp,
		mp_ptr rp,
		mp_srcptr np, mp_size_t nn,
		mp_srcptr dp, mp_size_t dn,
		mp_ptr scratch)
{
  ASSERT_ALWAYS (0);
}
