/* mpc_exp2 -- base-2 exponential of a complex number.

Copyright (C) 2024 INRIA

This file is part of GNU MPC.

GNU MPC is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

GNU MPC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this program. If not, see http://www.gnu.org/licenses/ .
*/

#include "mpc-impl.h"

int
mpc_exp2 (mpc_ptr rop, mpc_srcptr op, mpc_rnd_t rnd)
{
  mpc_t two;
  int ret;
  mpc_init2 (two, MPFR_PREC_MIN); // 1 bit is enough to store 2 exactly
  mpc_set_ui (two, 2, MPC_RNDNN);
  ret = mpc_pow (rop, two, op, rnd);
  mpc_clear (two);
  return ret;
}
