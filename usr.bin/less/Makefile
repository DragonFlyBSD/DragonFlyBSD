# $FreeBSD: src/usr.bin/less/Makefile,v 1.4.2.5 2003/02/02 10:04:50 johan Exp $
# $DragonFly: src/usr.bin/less/Makefile,v 1.5 2004/04/21 16:59:51 drhodus Exp $

PROG=	less
SRCS=	main.c screen.c brac.c ch.c charset.c cmdbuf.c \
	command.c decode.c edit.c filename.c forwback.c \
	help.c ifile.c input.c jump.c line.c linenum.c \
	lsystem.c mark.c optfunc.c option.c opttbl.c os.c \
	output.c position.c prompt.c search.c signal.c \
	tags.c ttyin.c version.c
CFLAGS=	-I${.CURDIR} -DSYSDIR="/etc"
DPADD=	${LIBTERMCAP}
LDADD=	-ltermcap
LINKS=	${BINDIR}/less ${BINDIR}/more
MLINKS=	less.1 more.1
CLEANFILES= less.1

.include <Makefile.common>
.include <bsd.prog.mk>
