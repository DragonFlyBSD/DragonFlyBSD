#
#      The FreeBSD Documentation Project
#      The FreeBSD French Documentation Project
#
#      $Id$
#      $FreeBSD: src/release/doc/fr_FR.ISO8859-1/installation/Makefile.inc,v 1.1.2.2 2002/11/01 20:52:55 gioria Exp $
#      $DragonFly: src/release/doc/fr_FR.ISO8859-1/installation/Attic/Makefile.inc,v 1.2 2003/06/17 04:27:19 dillon Exp $
#      Original revision: 1.1.2.2


.ifdef NO_LANGCODE_IN_DESTDIR
DESTDIR?=	${DOCDIR}/installation/${.CURDIR:T}
.else
DESTDIR?=	${DOCDIR}/fr_FR.ISO8859-1/installation/${.CURDIR:T}
.endif
