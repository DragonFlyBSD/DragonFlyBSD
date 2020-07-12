/*
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_MODULEPARAM_H_
#define _LINUX_MODULEPARAM_H_

#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/kernel.h>

#define MODULE_PARM_DESC(name, desc)

#define _TUNABLE_PREFIX			__stringify(KBUILD_MODNAME)
#define _TUNABLE_VAR(name)		_TUNABLE_PREFIX#name

#define TUNABLE_int(name, var)		TUNABLE_INT( _TUNABLE_VAR(.name), &(var))

#define TUNABLE_bool(name, var)		TUNABLE_INT( _TUNABLE_VAR(.name), (int *)&(var))

#define TUNABLE_uint(name, var)		TUNABLE_INT( _TUNABLE_VAR(.name), (int *)&(var))

#define TUNABLE_charp(name, var)	/* kgetenv() could be useful here */

#define module_param(var, type, mode) \
	TUNABLE_##type(name, var)

#define module_param_unsafe(var, type, mode)	module_param(var, type, mode)

#define module_param_named(name, var, type, mode) \
	TUNABLE_##type(name, var)

#define module_param_named_unsafe(name, var, type, mode) \
	TUNABLE_##type((name), (var))

#endif	/* _LINUX_MODULEPARAM_H_ */
