/* mpfr_eq -- Compare two floats up to a specified bit #.

Copyright 1999, 2001, 2003, 2004, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
Contributed by the Arenaire and Cacao projects, INRIA.

This file is part of the GNU MPFR Library.

The GNU MPFR Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

The GNU MPFR Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the GNU MPFR Library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
MA 02110-1301, USA. */


#include "mpfr-impl.h"

/* return non-zero if the first n_bits bits of u, v are equal,
   0 otherwise */
int
mpfr_eq (mpfr_srcptr u, mpfr_srcptr v, unsigned long int n_bits)
{
  mp_srcptr up, vp;
  mp_size_t usize, vsize, size, i;
  mp_exp_t uexp, vexp;
  int k;

  if (MPFR_ARE_SINGULAR(u, v))
    {
      if (MPFR_IS_NAN(u) || MPFR_IS_NAN(v))
        return 0; /* non equal */
      else if (MPFR_IS_INF(u) && MPFR_IS_INF(v))
        return (MPFR_SIGN(u) == MPFR_SIGN(v));
      else if (MPFR_IS_ZERO(u) && MPFR_IS_ZERO(v))
        return 1;
      else
        return 0;
    }

  /* 1. Are the signs different?  */
  if (MPFR_SIGN(u) != MPFR_SIGN(v))
    return 0;

  uexp = MPFR_GET_EXP (u);
  vexp = MPFR_GET_EXP (v);

  /* 2. Are the exponents different?  */
  if (uexp != vexp)
    return 0; /* no bit agree */

  usize = (MPFR_PREC(u) - 1) / BITS_PER_MP_LIMB + 1;
  vsize = (MPFR_PREC(v) - 1) / BITS_PER_MP_LIMB + 1;

  if (vsize > usize) /* exchange u and v */
    {
      up = MPFR_MANT(v);
      vp = MPFR_MANT(u);
      size = vsize;
      vsize = usize;
      usize = size;
    }
  else
    {
      up = MPFR_MANT(u);
      vp = MPFR_MANT(v);
    }

  /* now usize >= vsize */
  MPFR_ASSERTD(usize >= vsize);

  if (usize > vsize)
    {
      if ((unsigned long) vsize * BITS_PER_MP_LIMB < n_bits)
        {
          /* check if low min(PREC(u), n_bits) - (vsize * BITS_PER_MP_LIMB)
             bits from u are non-zero */
          unsigned long remains = n_bits - (vsize * BITS_PER_MP_LIMB);
          k = usize - vsize - 1;
          while (k >= 0 && remains >= BITS_PER_MP_LIMB && !up[k])
            {
              k--;
              remains -= BITS_PER_MP_LIMB;
            }
          /* now either k < 0: all low bits from u are zero
                 or remains < BITS_PER_MP_LIMB: check high bits from up[k]
                 or up[k] <> 0: different */
          if (k >= 0 && (((remains < BITS_PER_MP_LIMB) &&
                          (up[k] >> (BITS_PER_MP_LIMB - remains))) ||
                         (remains >= BITS_PER_MP_LIMB && up[k])))
            return 0;           /* surely too different */
        }
      size = vsize;
    }
  else
    {
      size = usize;
    }

  /* now size = min (usize, vsize) */

  /* If size is too large wrt n_bits, reduce it to look only at the
     high n_bits bits.
     Otherwise, if n_bits > size * BITS_PER_MP_LIMB, reduce n_bits to
     size * BITS_PER_MP_LIMB, since the extra low bits of one of the
     operands have already been check above. */
  if ((unsigned long) size > 1 + (n_bits - 1) / BITS_PER_MP_LIMB)
    size = 1 + (n_bits - 1) / BITS_PER_MP_LIMB;
  else if (n_bits > (unsigned long) size * BITS_PER_MP_LIMB)
    n_bits = size * BITS_PER_MP_LIMB;

  up += usize - size;
  vp += vsize - size;

  for (i = size - 1; i > 0 && n_bits >= BITS_PER_MP_LIMB; i--)
    {
      if (up[i] != vp[i])
        return 0;
      n_bits -= BITS_PER_MP_LIMB;
    }

  /* now either i=0 or n_bits<BITS_PER_MP_LIMB */

  /* since n_bits <= size * BITS_PER_MP_LIMB before the above for-loop,
     we have the invariant n_bits <= (i+1) * BITS_PER_MP_LIMB, thus
     we always have n_bits <= BITS_PER_MP_LIMB here */
  MPFR_ASSERTD(n_bits <= BITS_PER_MP_LIMB);

  if (n_bits & (BITS_PER_MP_LIMB - 1))
    return (up[i] >> (BITS_PER_MP_LIMB - (n_bits & (BITS_PER_MP_LIMB - 1))) ==
            vp[i] >> (BITS_PER_MP_LIMB - (n_bits & (BITS_PER_MP_LIMB - 1))));
  else
    return (up[i] == vp[i]);
}
