# $FreeBSD: src/usr.sbin/named/Makefile,v 1.25.2.5 2002/07/07 08:17:07 dougb Exp $
# $DragonFly: src/usr.sbin/named/Makefile,v 1.4 2004/05/27 18:15:42 dillon Exp $

USE_LIBBIND=	true
.include "${.CURDIR}/Makefile.inc"

.PATH: ${BIND_DIR}/bin/named
.PATH: ${BIND_DIR}/bin/nsupdate
.PATH: ${BIND_DIR}/doc/man

PROG=	named
# named.conf.5 named-bootconf.8
MAN=	named.8 nsupdate.8
SRCS=	aclconf.c client.c config.c control.c controlconf.c interfacemgr.c \
	listenlist.c log.c logconf.c main.c notify.c \
	query.c server.c sortlist.c \
	tkeyconf.c tsigconf.c update.c xfrout.c \
	zoneconf.c \
	lwaddr.c lwresd.c lwdclient.c lwderror.c lwdgabn.c \
	lwdgnba.c lwdgrbn.c lwdnoop.c lwsearch.c \

.PATH: ${BIND_DIR}/bin/named/unix
SRCS+=	os.c

CFLAGS+= -I${BIND_DIR}/bin/named/include

.if exists(${.OBJDIR}/../../lib/libisc)
LIBISCDIR:=    ${.OBJDIR}/../../lib/libisc
.else
LIBISCDIR!=    cd ${.CURDIR}/../../lib/libisc; make -V .OBJDIR
.endif
LIBISC:=       ${LIBISCDIR}/libisc.a

DPADD+=         ${LIBISC}
LDADD+=         ${LIBISC}

#HTMLS=	acl.html address_list.html comments.html config.html controls.html \
#	docdef.html example.html include.html index.html key.html \
#	logging.html master.html options.html server.html trusted-keys.html \
#	zone.html
HTMLS=
#MISCS=	DynamicUpdate FAQ.1of2 FAQ.2of2 rfc2317-notes.txt style.txt
MISCS=
FILES=	${HTMLS} ${MISCS}
.PATH: ${BIND_DIR}/doc/html ${BIND_DIR}/doc/misc

.for file in ${HTMLS}
FILESDIR_${file}=	${DOCDIR}/bind/html
.endfor
.for file in ${MISCS}
FILESDIR_${file}=	${DOCDIR}/bind/misc
.endfor

.include <bsd.prog.mk>
