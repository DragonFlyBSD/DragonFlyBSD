#	$OpenBSD: Makefile,v 1.4 2003/04/10 22:42:29 millert Exp $

SUBDIR=		boggle

# Only if building the dictionary
.if !defined(NO_SHARE)
SUBDIR+=	mkdict mkindex dictfiles
.endif

.include <bsd.subdir.mk>
