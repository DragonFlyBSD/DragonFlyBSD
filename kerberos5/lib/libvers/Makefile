# $FreeBSD: src/kerberos5/lib/libvers/Makefile,v 1.3.2.2 2002/07/19 18:46:25 ru Exp $
# $DragonFly: src/kerberos5/lib/libvers/Makefile,v 1.2 2003/06/17 04:26:17 dillon Exp $

LIB=		vers
INTERNALLIB=	YES

CFLAGS+=	-I${KRB5DIR}/include \
		-I${ROKENOBJDIR} \
		-I${KRB5DIR}/lib/roken \
		-I${.OBJDIR}

SRCS=		\
		print_version.c	\
		print_version.h

.include <bsd.lib.mk>

.PATH:		${KRB5DIR}/lib/vers

build-tools:	make-print-version

print_version.h: make-print-version
	./make-print-version print_version.h

make-print-version: make-print-version.c
	${CC} ${CFLAGS} -static -o ${.TARGET} ${.OODATE}

CLEANFILES+=	make-print-version print_version.h
