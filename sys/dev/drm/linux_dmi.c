/*
 * Copyright (c) 2019 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/types.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/dmi.h>
#include <asm/unaligned.h>

bool
dmi_match(enum dmi_field f, const char *str)
{
	const char *s = NULL;

	switch (f) {
	case DMI_NONE:
		break;
	case DMI_SYS_VENDOR:
		s = kgetenv("smbios.system.maker");
		if (s != NULL && !strcmp(s, str))
			return true;
		break;
	case DMI_BOARD_VENDOR:
		s = kgetenv("smbios.planar.maker");
		if (s != NULL && !strcmp(s, str))
			return true;
		break;
	case DMI_PRODUCT_NAME:
		s = kgetenv("smbios.system.product");
		if (s != NULL && !strcmp(s, str))
			return true;
		break;
	case DMI_BOARD_NAME:
		s = kgetenv("smbios.planar.product");
		if (s != NULL && !strcmp(s, str))
			return true;
		break;
	default:
		return false;
	}

	return false;
}
