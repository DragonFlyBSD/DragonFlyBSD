# $FreeBSD: src/gnu/lib/libreadline/readline/doc/Makefile,v 1.2.2.1 2000/07/06 23:11:11 ache Exp $
# $DragonFly: src/gnu/lib/libreadline/readline/doc/Attic/Makefile,v 1.2 2003/06/17 04:25:44 dillon Exp $

SRCDIR= ${.CURDIR}/../../../../../contrib/libreadline/doc

INFO = readline rluserman

INFOSECTION= "Programming & development tools."
INFOENTRY_readline= "* Readline: (readline).		The GNU Readline library"
INFOENTRY_rluserman= "* Readline: (readline).       The GNU readline library API"

CLEANFILES += readline.texinfo

readline.info: rlman.texinfo rltech.texinfo rluser.texinfo manvers.texinfo

readline.texinfo: rlman.texinfo
	cp ${SRCDIR}/rlman.texinfo ${.TARGET}

rluserman.info: rluser.texinfo manvers.texinfo

.include <bsd.info.mk>
