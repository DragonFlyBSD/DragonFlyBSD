# $FreeBSD: src/release/doc/ja_JP.eucJP/relnotes/Makefile.inc,v 1.2.2.1 2001/09/24 12:51:37 hrs Exp $
# $DragonFly: src/release/doc/ja_JP.eucJP/relnotes/Attic/Makefile.inc,v 1.2 2003/06/17 04:27:20 dillon Exp $
# Original revision: 1.1.2.2

.ifdef NO_LANGCODE_IN_DESTDIR
DESTDIR?=	${DOCDIR}/relnotes/${.CURDIR:T}
.else
DESTDIR?=	${DOCDIR}/ja_JP.eucJP/relnotes/${.CURDIR:T}
.endif

