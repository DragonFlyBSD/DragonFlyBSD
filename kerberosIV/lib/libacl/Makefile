# $FreeBSD: src/kerberosIV/lib/libacl/Makefile,v 1.4.2.2 2002/07/22 14:21:48 ru Exp $
# $DragonFly: src/kerberosIV/lib/libacl/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

LIB=		acl
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/acl \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/lib/roken \
		-I${INCLUDEOBJDIR} \
		-I${.OBJDIR}
SRCS=		acl_files.c protos.h
INCS=		acl.h
MAN=		${KRB4DIR}/man/acl_check.3
MLINKS=		acl_check.3 acl_canonicalize_principal.3 \
		acl_check.3 acl_exact_match.3 \
		acl_check.3 acl_add.3 \
		acl_check.3 acl_delete.3 \
		acl_check.3 acl_initialize.3

.include <bsd.lib.mk>

.PATH:		${KRB4DIR}/lib/acl

CLEANFILES+=	protos.h
