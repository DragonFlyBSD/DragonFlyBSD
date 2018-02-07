# Makefile for /bin/sh
#

PROG=	sh
INSTALLFLAGS= -S
SHSRCS=	alias.c arith_yacc.c arith_yylex.c cd.c echo.c error.c eval.c \
	exec.c expand.c \
	histedit.c input.c jobs.c kill.c mail.c main.c memalloc.c miscbltin.c \
	mystring.c options.c output.c parser.c printf.c redir.c show.c \
	test.c trap.c var.c
GENSRCS= builtins.c nodes.c syntax.c
GENHDRS= builtins.h nodes.h syntax.h token.h
SRCS= ${SHSRCS} ${GENSRCS} ${GENHDRS}

# would be needed if WARNS upgraded to 3, but lets keep the stderr output
# clean, so WARNS has been lowered to 2.
#
# NO_WERROR=	yes # [-Werror=clobbered] on gcc50
WARNS?= 2

# MLINKS for Shell built in commands for which there are no userland
# utilities of the same name are handled with the associated manpage,
# builtin.1 in share/man/man1/.

CFLAGS+=-DSHELL -I. -I${.CURDIR}
# for debug:
# DEBUG_FLAGS+= -g -DDEBUG=2 -fno-inline

.if defined(BOOTSTRAPPING)
CFLAGS+= -DNO_HISTORY
# LIBEDIT is not needed here
.else
DPADD= ${LIBEDIT} ${LIBNCURSES}
LDADD= ${LIBEDIT} ${LIBNCURSES}
.endif


.PATH:	${.CURDIR}/bltin \
	${.CURDIR}/../kill \
	${.CURDIR}/../test \
	${.CURDIR}/../../usr.bin/printf

#
# NOTE: the headers and sources in pregenerated/ must be updated upon any
# changes to the nodetypes nodes.c.pat mkbuiltins mknodes.c mksyntax.c mktokens
# files using following scripts and build programs.
#
.if 1 # not updating sh(1)
.PATH: ${.CURDIR}/pregenerated
CFLAGS+= -I${.CURDIR}/pregenerated
.else
CLEANFILES+= ${GENSRCS} ${GENHDRS}

.ORDER: builtins.c builtins.h
builtins.c builtins.h: mkbuiltins builtins.def
	sh ${.CURDIR}/mkbuiltins ${.CURDIR}

# XXX this is just to stop the default .c rule being used, so that the
# intermediate object has a fixed name.
# XXX we have a default .c rule, but no default .o rule.
.o:
	${CC} ${CFLAGS} ${LDFLAGS} ${.IMPSRC} ${LDLIBS} -o ${.TARGET}

.ORDER: nodes.c nodes.h
nodes.c nodes.h: nodetypes nodes.c.pat
	${.OBJDIR}/mknodes/mknodes.nx ${.ALLSRC}

.ORDER: syntax.c syntax.h
syntax.c syntax.h:
	${.OBJDIR}/mksyntax/mksyntax.nx

token.h: mktokens
	sh ${.CURDIR}/mktokens
.endif

regress: sh
	cd ${.CURDIR}/../../tools/regression/bin/sh && ${MAKE} SH=${.OBJDIR}/sh
.include <bsd.prog.mk>
