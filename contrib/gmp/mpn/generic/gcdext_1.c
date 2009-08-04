/* mpn_gcdext -- Extended Greatest Common Divisor.

Copyright 1996, 1998, 2000, 2001, 2002, 2003, 2004, 2005, 2008 Free Software
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

/* Default to binary gcdext_1, since it is best on most current machines.
   We should teach tuneup to choose the right gcdext_1.  */
#define GCDEXT_1_USE_BINARY 1

#include "gmp.h"
#include "gmp-impl.h"
#include "longlong.h"

#ifndef NULL
# define NULL ((void *) 0)
#endif

/* FIXME: Takes two single-word limbs. It could be extended to a
 * function that accepts a bignum for the first input, and only
 * returns the first co-factor. */

/* Returns g, u and v such that g = u A - v B. There are three
   different cases for the result:

     g = u A - v B, 0 < u < b, 0 < v < a
     g = A          u = 1, v = 0
     g = B          u = B, v = A - 1

   We always return with 0 < u <= b, 0 <= v < a.
*/
#if GCDEXT_1_USE_BINARY

static mp_limb_t
gcdext_1_odd (mp_limb_t *up, mp_limb_t *vp, mp_limb_t a, mp_limb_t b)
{
  mp_limb_t u0;
  mp_limb_t v0;
  mp_limb_t v1;
  mp_limb_t u1;

  mp_limb_t B = b;
  mp_limb_t A = a;

  /* Through out this function maintain

     a = u0 A - v0 B
     b = u1 A - v1 B

     where A and B are odd. */

  u0 = 1; v0 = 0;
  u1 = b; v1 = a-1;

  if (A == 1)
    {
      *up = u0; *vp = v0;
      return 1;
    }
  else if (B == 1)
    {
      *up = u1; *vp = v1;
      return 1;
    }

  while (a != b)
    {
      mp_limb_t mask;

      ASSERT (a % 2 == 1);
      ASSERT (b % 2 == 1);

      ASSERT (0 < u0); ASSERT (u0 <= B);
      ASSERT (0 < u1); ASSERT (u1 <= B);

      ASSERT (0 <= v0); ASSERT (v0 < A);
      ASSERT (0 <= v1); ASSERT (v1 < A);

      if (a > b)
	{
	  MP_LIMB_T_SWAP (a, b);
	  MP_LIMB_T_SWAP (u0, u1);
	  MP_LIMB_T_SWAP (v0, v1);
	}

      ASSERT (a < b);

      /* Makes b even */
      b -= a;

      mask = - (mp_limb_t) (u1 < u0);
      u1 += B & mask;
      v1 += A & mask;
      u1 -= u0;
      v1 -= v0;

      ASSERT (b % 2 == 0);

      do
	{
	  /* As b = u1 A + v1 B is even, while A and B are odd,
	     either both or none of u1, v1 is even */

	  ASSERT (u1 % 2 == v1 % 2);

	  mask = -(u1 & 1);
	  u1 = u1 / 2 + ((B / 2) & mask) - mask;
	  v1 = v1 / 2 + ((A / 2) & mask) - mask;

	  b /= 2;
	}
      while (b % 2 == 0);
    }

  /* Now g = a = b */
  ASSERT (a == b);
  ASSERT (u1 <= B);
  ASSERT (v1 < A);

  ASSERT (A % a == 0);
  ASSERT (B % a == 0);
  ASSERT (u0 % (B/a) == u1 % (B/a));
  ASSERT (v0 % (A/a) == v1 % (A/a));

  *up = u0; *vp = v0;

  return a;
}

mp_limb_t
mpn_gcdext_1 (mp_limb_t *up, mp_limb_t *vp, mp_limb_t a, mp_limb_t b)
{
  unsigned shift = 0;
  mp_limb_t g;
  mp_limb_t u;
  mp_limb_t v;

  /* We use unsigned values in the range 0, ... B - 1. As the values
     are uniquely determined only modulo B, we can add B at will, to
     get numbers in range or flip the least significant bit. */
  /* Deal with powers of two */
  while ((a | b) % 2 == 0)
    {
      a /= 2; b /= 2; shift++;
    }

  if (b % 2 == 0)
    {
      unsigned k = 0;

      do {
	b /= 2; k++;
      } while (b % 2 == 0);

      g = gcdext_1_odd (&u, &v, a, b);

      while (k--)
	{
	  /* We have g = u a + v b, and need to construct
	     g = u'a + v'(2b).

	     If v is even, we can just set u' = u, v' = v/2
	     If v is odd, we can set v' = (v + a)/2, u' = u + b
	  */

	  if (v % 2 == 0)
	    v /= 2;
	  else
	    {
	      u = u + b;
	      v = v/2 + a/2 + 1;
	    }
	  b *= 2;
	}
    }
  else if (a % 2 == 0)
    {
      unsigned k = 0;

      do {
	a /= 2; k++;
      } while (a % 2 == 0);

      g = gcdext_1_odd (&u, &v, a, b);

      while (k--)
	{
	  /* We have g = u a + v b, and need to construct
	     g = u'(2a) + v'b.

	     If u is even, we can just set u' = u/2, v' = v.
	     If u is odd, we can set u' = (u + b)/2
	  */

	  if (u % 2 == 0)
	    u /= 2;
	  else
	    {
	      u = u/2 + b/2 + 1;
	      v = v + a;
	    }
	  a *= 2;
	}
    }
  else
    /* Ok, both are odd */
    g = gcdext_1_odd (&u, &v, a, b);

  *up = u;
  *vp = v;

  return g << shift;
}

#else /* ! GCDEXT_1_USE_BINARY */
static mp_limb_t
gcdext_1_u (mp_limb_t *up, mp_limb_t a, mp_limb_t b)
{
  /* Maintain

     a =   u0 A mod B
     b = - u1 A mod B
  */
  mp_limb_t u0 = 1;
  mp_limb_t u1 = 0;
  mp_limb_t B = b;

  ASSERT (a >= b);
  ASSERT (b > 0);

  for (;;)
    {
      mp_limb_t q;

      q = a / b;
      a -= q * b;

      if (a == 0)
	{
	  *up = B - u1;
	  return b;
	}
      u0 += q * u1;

      q = b / a;
      b -= q * a;

      if (b == 0)
	{
	  *up = u0;
	  return a;
	}
      u1 += q * u0;
    }
}

mp_limb_t
mpn_gcdext_1 (mp_limb_t *up, mp_limb_t *vp, mp_limb_t a, mp_limb_t b)
{
  /* Maintain

     a =   u0 A - v0 B
     b = - u1 A + v1 B = (B - u1) A - (A - v1) B
  */
  mp_limb_t u0 = 1;
  mp_limb_t v0 = 0;
  mp_limb_t u1 = 0;
  mp_limb_t v1 = 1;

  mp_limb_t A = a;
  mp_limb_t B = b;

  ASSERT (a >= b);
  ASSERT (b > 0);

  for (;;)
    {
      mp_limb_t q;

      q = a / b;
      a -= q * b;

      if (a == 0)
	{
	  *up = B - u1;
	  *vp = A - v1;
	  return b;
	}
      u0 += q * u1;
      v0 += q * v1;

      q = b / a;
      b -= q * a;

      if (b == 0)
	{
	  *up = u0;
	  *vp = v0;
	  return a;
	}
      u1 += q * u0;
      v1 += q * v0;
    }
}
#endif /* ! GCDEXT_1_USE_BINARY */
