# $FreeBSD: src/usr.bin/bzip2/Makefile,v 1.5.2.2 2001/08/02 01:09:06 obrien Exp $
# $DragonFly: src/usr.bin/bzip2/Makefile,v 1.5.2.1 2006/01/01 00:55:08 dillon Exp $

BZ2DIR=	${.CURDIR}/../../contrib/bzip2-1.0
CONTRIBDIR= ${BZ2DIR}

.PATH: ${BZ2DIR}

PROG=	bzip2
SRCS=	bzip2.c bzip2.c.patch

MAN=	bzip2.1
CFLAGS+= -D_FILE_OFFSET_BITS=64

DPADD=	${LIBBZ2}
LDADD=	-lbz2

NOSHARED?=	YES

LINKS=	${BINDIR}/bzip2 ${BINDIR}/bunzip2
LINKS+=	${BINDIR}/bzip2 ${BINDIR}/bzcat
MLINKS=	bzip2.1 bunzip2.1 bzip2.1 bzcat.1 bzip2.1 bzip2recover.1

REFFILES=	sample1.ref sample2.ref sample3.ref 
DREFFILES=	sample1.bz2 sample2.bz2 sample3.bz2 
TESTFILES=	${REFFILES} ${DREFFILES}

CLEANFILES+=	sample1.rb2 sample2.rb2 sample3.rb2 \
		sample1.tst sample2.tst sample3.tst

test:  bzip2 ${TESTFILES}
	@cat ${BZ2DIR}/words1
	./bzip2 -1  < ${BZ2DIR}/sample1.ref > sample1.rb2
	./bzip2 -2  < ${BZ2DIR}/sample2.ref > sample2.rb2
	./bzip2 -3  < ${BZ2DIR}/sample3.ref > sample3.rb2
	./bzip2 -d  < ${BZ2DIR}/sample1.bz2 > sample1.tst
	./bzip2 -d  < ${BZ2DIR}/sample2.bz2 > sample2.tst
	./bzip2 -ds < ${BZ2DIR}/sample3.bz2 > sample3.tst
	cmp ${BZ2DIR}/sample1.bz2 sample1.rb2 
	cmp ${BZ2DIR}/sample2.bz2 sample2.rb2
	cmp ${BZ2DIR}/sample3.bz2 sample3.rb2
	cmp sample1.tst ${BZ2DIR}/sample1.ref
	cmp sample2.tst ${BZ2DIR}/sample2.ref
	cmp sample3.tst ${BZ2DIR}/sample3.ref
	@cat ${BZ2DIR}/words3

.include <bsd.prog.mk>
