#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/share/Makefile,v 1.22.2.4 2002/03/12 17:13:32 phantom Exp $
# $DragonFly: src/share/Makefile,v 1.3 2005/03/13 18:05:53 joerg Exp $

# Do not include `info' in the SUBDIR list, it is handled separately.

SUBDIR= colldef dict examples man me misc mk mklocale monetdef \
	msgdef numericdef skel syscons tabset termcap timedef zoneinfo

.if !defined(NO_I4B)
SUBDIR+= isdn
.endif

.if !defined(NO_SENDMAIL)
SUBDIR+= sendmail
.endif

.include <bsd.subdir.mk>
