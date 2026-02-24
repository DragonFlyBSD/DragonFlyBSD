/*
 * Copyright (c) 2005-2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_SERIALIZE2_H_
#define _SYS_SERIALIZE2_H_

#ifndef _KERNEL
#error "kernel only header file"
#endif

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif

#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif

static __inline void
lwkt_serialize_array_enter(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_s < _arrcnt, ("nothing to be serialized"));
	while (_s < _arrcnt)
		lwkt_serialize_enter(_arr[_s++]);
}

static __inline int
lwkt_serialize_array_try(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	int _i;

	KASSERT(_s < _arrcnt, ("nothing to be serialized"));
	for (_i = _s; _i < _arrcnt; ++_i) {
		if (!lwkt_serialize_try(_arr[_i])) {
			while (--_i >= _s)
				lwkt_serialize_exit(_arr[_i]);
			return 0;
		}
	}
	return 1;
}

static __inline void
lwkt_serialize_array_exit(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_arrcnt > _s, ("nothing to be deserialized"));
	while (--_arrcnt >= _s)
		lwkt_serialize_exit(_arr[_arrcnt]);
}

#endif	/* !_SYS_SERIALIZE2_H_ */
