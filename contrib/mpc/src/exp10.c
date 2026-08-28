/* mpc_exp10 -- base-10 exponential of a complex number.

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
mpc_exp10 (mpc_ptr rop, mpc_srcptr op, mpc_rnd_t rnd)
{
  mpc_t ten;
  int ret;
  mpc_init2 (ten, 4); // 4 bits is enough to store 10 exactly
  mpc_set_ui (ten, 10, MPC_RNDNN);
  ret = mpc_pow (rop, ten, op, rnd);
  mpc_clear (ten);
  return ret;
}
