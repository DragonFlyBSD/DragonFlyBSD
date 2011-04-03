/* mpn_gcdext -- Extended Greatest Common Divisor.

Copyright 1996, 1998, 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009 Free Software
Foundation, Inc.

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
#include "longlong.h"


/* FIXME: Takes two single-word limbs. It could be extended to a
 * function that accepts a bignum for the first input, and only
 * returns the first co-factor. */

mp_limb_t
mpn_gcdext_1 (mp_limb_signed_t *up, mp_limb_signed_t *vp,
	      mp_limb_t a, mp_limb_t b)
{
  /* Maintain

     a =  u0 A + v0 B
     b =  u1 A + v1 B

     where A, B are the original inputs.
  */
  mp_limb_signed_t u0 = 1;
  mp_limb_signed_t v0 = 0;
  mp_limb_signed_t u1 = 0;
  mp_limb_signed_t v1 = 1;

  ASSERT (a > 0);
  ASSERT (b > 0);

  if (a < b)
    goto divide_by_b;

  for (;;)
    {
      mp_limb_t q;

      q = a / b;
      a -= q * b;

      if (a == 0)
	{
	  *up = u1;
	  *vp = v1;
	  return b;
	}
      u0 -= q * u1;
      v0 -= q * v1;

    divide_by_b:
      q = b / a;
      b -= q * a;

      if (b == 0)
	{
	  *up = u0;
	  *vp = v0;
	  return a;
	}
      u1 -= q * u0;
      v1 -= q * v0;
    }
}
