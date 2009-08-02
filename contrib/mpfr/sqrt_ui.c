/* mpfr_sqrt_ui -- square root of a machine integer

Copyright 2000, 2001, 2002, 2003, 2004, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
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

#define MPFR_NEED_LONGLONG_H
#include "mpfr-impl.h"

int
mpfr_sqrt_ui (mpfr_ptr r, unsigned long u, mp_rnd_t rnd_mode)
{
  if (u)
    {
      mpfr_t uu;
      mp_limb_t up[1];
      unsigned long cnt;
      int inex;
      MPFR_SAVE_EXPO_DECL (expo);

      MPFR_TMP_INIT1 (up, uu, BITS_PER_MP_LIMB);
      MPFR_ASSERTN (u == (mp_limb_t) u);
      count_leading_zeros (cnt, (mp_limb_t) u);
      *up = (mp_limb_t) u << cnt;

      MPFR_SAVE_EXPO_MARK (expo);
      MPFR_SET_EXP (uu, BITS_PER_MP_LIMB - cnt);
      inex = mpfr_sqrt(r, uu, rnd_mode);
      MPFR_SAVE_EXPO_FREE (expo);
      return mpfr_check_range(r, inex, rnd_mode);
    }
  else /* sqrt(0) = 0 */
    {
      MPFR_SET_ZERO(r);
      MPFR_SET_POS(r);
      MPFR_RET(0);
    }
}
