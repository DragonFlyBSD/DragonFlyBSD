#	$DragonFly: src/usr.sbin/pcvt/kbdio/Attic/Makefile,v 1.2 2004/03/31 20:22:13 cpressey Exp $

.if !exists(${.CURDIR}/../Makefile.inc)
error:
	@echo
	@echo "                      You MUST link/copy"
	@echo
	@echo "       ../Makefile.inc.NetBSD    or   ../Makefile.inc.FreeBSD"
	@echo
	@echo "             to ../Makefile.inc before you can proceed !"
	@echo
.else

PROG=	kbdio
SRCS=	kbdio.y lex.l y.tab.h

#YACC=	bison
#YFLAGS+= -yd	# Bison only

YFLAGS+= -v	# verbose

LFLAGS+= -I

DPADD=	${LIBM} ${LIBL}
LDADD=	-lm -ll

NOMAN=

CFLAGS+= -I${.OBJDIR} -I${.CURDIR} #-g

CLEANFILES+= y.output	# comment file from bison

install:
	@${ECHO} "kbdio is not installed automatically ...."

.include <bsd.prog.mk>

.endif
