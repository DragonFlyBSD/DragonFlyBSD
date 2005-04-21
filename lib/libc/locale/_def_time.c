/*	$NetBSD: src/lib/libc/locale/_def_time.c,v 1.7 2003/07/26 19:24:46 salo Exp $	*/
/*	$DragonFly: src/lib/libc/locale/_def_time.c,v 1.2 2005/04/21 16:36:34 joerg Exp $ */

/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <sys/localedef.h>
#include <locale.h>

const _TimeLocale _DefaultTimeLocale =  {
	{ "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
	{ "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
	  "Friday", "Saturday" },
	{ "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
	{ "January", "February", "March", "April", "May", "June",
	  "July", "August", "September", "October", "November", "December" },
	{ "AM", "PM" },
	"%a %b %e %H:%M:%S %Y",
	"%m/%d/%y",
	"%H:%M:%S",
	"%I:%M:%S %p",
	"%a %b %e %H:%M:%S %Z %Y"
};

const _TimeLocale *_CurrentTimeLocale = &_DefaultTimeLocale;
