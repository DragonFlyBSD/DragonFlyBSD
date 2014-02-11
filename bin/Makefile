#	From: @(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/Makefile,v 1.15.2.2 2000/10/31 09:43:57 dougb Exp $
# $DragonFly: src/bin/Makefile,v 1.10 2008/05/27 17:10:48 dillon Exp $

SUBDIR= cat \
	chio \
	chmod \
	cp \
	cpdup \
	csh \
	date \
	dd \
	df \
	domainname \
	echo \
	ed \
	expr \
	hostname \
	kill \
	ln \
	ls \
	mined \
	mkdir \
	mv \
	notty \
	pax \
	ps \
	pwd \
	rcp \
	realpath \
	rm \
	rmdir \
	sh \
	sleep \
	stty \
	sync \
	test \
	uuidgen \
	varsym

.if !defined(NO_SENDMAIL)
SUBDIR+=rmail
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
