/*	$NetBSD: src/lib/libc/locale/_def_messages.c,v 1.5 2003/07/26 19:24:45 salo Exp $	*/
/*	$DragonFly: src/lib/libc/locale/_def_messages.c,v 1.1 2005/03/16 06:54:41 joerg Exp $ */

/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <sys/localedef.h>
#include <locale.h>

const _MessagesLocale _DefaultMessagesLocale = {
	"^[Yy]",
	"^[Nn]",
	"yes",
	"no"
};

const _MessagesLocale *_CurrentMessagesLocale = &_DefaultMessagesLocale;
