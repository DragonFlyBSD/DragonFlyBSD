# $FreeBSD: src/kerberos5/Makefile.inc,v 1.24 2004/12/21 09:33:44 ru Exp $
# $DragonFly: src/kerberos5/Makefile.inc,v 1.6 2005/01/16 14:25:45 eirikn Exp $

NO_LINT=

KRB5DIR=	${.CURDIR}/../../../crypto/heimdal-0.6.3

CFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR}/../../include
CFLAGS+=	-DINET6

.if defined(WITH_OPENLDAP)
OPENLDAPBASE?=	/usr/local
LDAPLDADD=	-lldap -llber
LDAPDPADD=	${LDAPLDADD:C;^-l(.*)$;${OPENLDAPBASE}/lib/lib\1.a;}
LDAPCFLAGS=	-I${OPENLDAPBASE}/include -DOPENLDAP=1
LDAPLDFLAGS=	-L${OPENLDAPBASE}/lib -Wl,-rpath,${OPENLDAPBASE}/lib
.endif

.if exists(${.OBJDIR}/../../lib/libvers)
LIBVERS=	${.OBJDIR}/../../lib/libvers/libvers.a
.else
LIBVERS=	${.CURDIR}/../../lib/libvers/libvers.a
.endif

.if exists(${.OBJDIR}/../../lib/libsl)
LIBSL=		${.OBJDIR}/../../lib/libsl/libsl.a
.else
LIBSL=		${.CURDIR}/../../lib/libsl/libsl.a
.endif

.if exists(${.OBJDIR}/../../tools/make-print-version/make-print-version)
MAKEPRINTVERSION=${.OBJDIR}/../../tools/make-print-version/make-print-version
.elif exists(${.OBJDIR}/../../bintools/make-print-version/make-print-version)
MAKEPRINTVERSION=${.OBJDIR}/../../bintools/make-print-version/make-print-version
.else
MAKEPRINTVERSION=make-print-version
.endif
.if exists(${.OBJDIR}/../../tools/make-roken/make-roken)
MAKEROKEN=	${.OBJDIR}/../../tools/make-roken/make-roken
.elif exists(${.OBJDIR}/../../bintools/make-roken/make-roken)
MAKEROKEN=	${.OBJDIR}/../../bintools/make-roken/make-roken
.else
MAKEROKEN=	make-roken
.endif
.if exists(${.OBJDIR}/../../tools/asn1_compile/asn1_compile)
ASN1COMPILE=	${.OBJDIR}/../../tools/asn1_compile/asn1_compile
.elif exists(${.OBJDIR}/../../bintools/asn1_compile/asn1_compile)
ASN1COMPILE=	${.OBJDIR}/../../bintools/asn1_compile/asn1_compile
.else
ASN1COMPILE=	asn1_compile
.endif

.if defined(SRCS)

ETSRCS=	\
	${KRB5DIR}/lib/asn1/asn1_err.et \
	${KRB5DIR}/lib/hdb/hdb_err.et \
	${KRB5DIR}/lib/kadm5/kadm5_err.et \
	${KRB5DIR}/lib/krb5/heim_err.et \
	${KRB5DIR}/lib/krb5/k524_err.et \
	${KRB5DIR}/lib/krb5/krb5_err.et

.for ET in ${ETSRCS}
.for _ET in ${ET:T:R}
.if ${SRCS:M${_ET}.[ch]} != ""
.ORDER: ${_ET}.c ${_ET}.h
${_ET}.c ${_ET}.h: ${ET}
	compile_et ${.ALLSRC}
CLEANFILES+=	${_ET}.h ${_ET}.c
.endif
.endfor
.endfor

.endif defined(SRCS)
