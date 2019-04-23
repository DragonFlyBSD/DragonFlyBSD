#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/share/Makefile,v 1.22.2.4 2002/03/12 17:13:32 phantom Exp $

SUBDIR= colldef \
	ctypedef \
	dict \
	doc \
	examples \
	i18n \
	locale-links \
	man \
	misc \
	mk \
	monetdef \
	msgdef \
	numericdef \
	skel \
	syscons \
	tabset \
	termcap \
	terminfo \
	timedef \
	zoneinfo

# maximum parallism
SUBDIR_ORDERED=

.if defined(WANT_INSTALLER)
SUBDIR+= installer
.endif

.include <bsd.subdir.mk>
