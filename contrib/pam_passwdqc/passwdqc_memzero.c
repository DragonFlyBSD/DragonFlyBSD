/*
 * Copyright (c) 2016 by Solar Designer.  See LICENSE.
 */

#ifdef _MSC_VER
#include <windows.h>
#else
#include <string.h>
#endif

static void memzero(void *buf, size_t len)
{
#ifdef _MSC_VER
	SecureZeroMemory(buf, len);
#else
	memset(buf, 0, len);
#endif
}

void (*_passwdqc_memzero)(void *, size_t) = memzero;
