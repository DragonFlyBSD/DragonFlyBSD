# $FreeBSD: src/gnu/lib/libreadline/Makefile.inc,v 1.4.2.1 2000/07/06 23:11:07 ache Exp $
# $DragonFly: src/gnu/lib/libreadline/Attic/Makefile.inc,v 1.3 2004/10/23 16:58:53 joerg Exp $

SHLIB_MAJOR=	5
RL_LIBRARY_VERSION=	5.0

RLDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/readline-5.0
SRCDIR= ${RLDIR}
.PATH: ${SRCDIR} ${.CURDIR}/..

LOCAL_CFLAGS= -DRL_LIBRARY_VERSION='"$(RL_LIBRARY_VERSION)"'
CFLAGS+= -I${.CURDIR}/.. -I${SRCDIR} -DHAVE_CONFIG_H ${LOCAL_CFLAGS}

HISTSRC=	history.c histexpand.c histfile.c histsearch.c mbutil.c
HISTSRC+=	shell.c xmalloc.c
