#	@(#)Makefile	8.1 (Berkeley) 7/2/95
# $FreeBSD: src/libexec/smrsh/Makefile,v 1.1.6.6 2002/03/25 21:31:40 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.14
.PATH:	${SENDMAIL_DIR}/smrsh

PROG=	smrsh
SRCS=	smrsh.c
MAN=	smrsh.8
WARNS?=	2

CFLAGS+=-I${SENDMAIL_DIR}/sendmail -I${SENDMAIL_DIR}/include -I.

.if exists(${.OBJDIR}/../../lib/libsm)
LIBSMDIR:=	${.OBJDIR}/../../lib/libsm
.else
LIBSMDIR!=	cd ${.CURDIR}/../../lib/libsm; make -V .OBJDIR
.endif
LIBSM:=		${LIBSMDIR}/libsm.a

DPADD=	${LIBSM}
LDADD=	${LIBSM}

SRCS+=	sm_os.h
CLEANFILES+=sm_os.h

PATCHES!=	echo ${.CURDIR}/patches/*.patch
.for patch in ${PATCHES}
.for file in ${patch:R:T}
CLEANFILES+=	${file}
${file}: ${SENDMAIL_DIR}/smrsh/${file} ${patch}
	patch -s -o ${.TARGET} -i ${patch} ${SENDMAIL_DIR}/smrsh/${file}
.endfor
.endfor

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}
DPADD+=${SENDMAIL_DPADD}
LDADD+=${SENDMAIL_LDADD}
LDFLAGS+=${SENDMAIL_LDFLAGS}

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.prog.mk>
