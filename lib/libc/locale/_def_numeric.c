/*	$NetBSD: src/lib/libc/locale/_def_numeric.c,v 1.5 2003/07/26 19:24:46 salo Exp $	*/
/*	$DragonFly: src/lib/libc/locale/_def_numeric.c,v 1.1 2005/03/16 06:54:41 joerg Exp $ */

/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <sys/localedef.h>
#include <locale.h>

const _NumericLocale _DefaultNumericLocale = {
	".",
	"",
	""
};

const _NumericLocale *_CurrentNumericLocale = &_DefaultNumericLocale;
