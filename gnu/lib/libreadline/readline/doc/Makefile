# $FreeBSD: src/gnu/lib/libreadline/readline/doc/Makefile,v 1.2.2.1 2000/07/06 23:11:11 ache Exp $
# $DragonFly: src/gnu/lib/libreadline/readline/doc/Attic/Makefile,v 1.3 2004/10/23 16:58:53 joerg Exp $

RELATIVE=	../
.include "../../Makefile.inc"
SRCDIR=		${RLDIR}/doc

INFO=	readline rluserman

INFOSECTION=		"Programming & development tools."
INFOENTRY_readline=	"* Readline: (readline).		The GNU Readline library"
INFOENTRY_rluserman=	"* Readline: (readline).		The GNU readline library API"

CLEANFILES += readline.texi 

readline.info: rlman.texi rltech.texi rluser.texi version.texi rluserman.texi

readline.texi: rlman.texi
	cp ${SRCDIR}/rlman.texi ${.TARGET}

rluserman.info: rluser.texi version.texi

.include <bsd.info.mk>
