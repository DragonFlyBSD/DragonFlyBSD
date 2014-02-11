#	From: @(#)Makefile	8.3 (Berkeley) 1/7/94
# $FreeBSD: src/usr.bin/Makefile,v 1.144.2.17 2003/01/04 17:17:07 obrien Exp $

# XXX MISSING:		deroff diction graph learn plot
#			spell spline struct xsend
# XXX broken:		tconv
# XXX Use GNU versions: apropos diff grep ld man ptx uucp whatis
# Moved to secure: bdes
#

# No ordering dependencies, everything can run in parallel
#
SUBDIR_ORDERED=

SUBDIR=	alias \
	apply \
	asa \
	at \
	awk \
	banner \
	basename \
	biff \
	bmake \
	brandelf \
	btpin \
	bzip2 \
	bzip2recover \
	c89 \
	c99 \
	calendar \
	cap_mkdb \
	catman \
	chat \
	checknr \
	checkpt \
	chflags \
	chkey \
	chpass \
	cksum \
	cmp \
	col \
	colcrt \
	colldef \
	colrm \
	column \
	comm \
	compile_et \
	compress \
	cpio \
	csplit \
	ctags \
	cut \
	dfregress \
	dirname \
	drill \
	du \
	ee \
	enigma \
	env \
	evtranalyze \
	expand \
	false \
	fetch \
	file \
	file2c \
	find \
	finger \
	flex \
	fmt \
	fold \
	from \
	fstat \
	fsync \
	ftp \
	gcore \
	gencat \
	getconf \
	getent \
	getopt \
	gzip \
	head \
	hexdump \
	iconv \
	id \
	indent \
	ioprio \
	ipcrm \
	ipcs \
	join \
	jot \
	kdump \
	kenv \
	keylogin \
	keylogout \
	killall \
	ktrace \
	ktrdump \
	lam \
	last \
	lastcomm \
	ldd \
	leave \
	less \
	lessecho \
	lesskey \
	limits \
	locale \
	locate \
	lock \
	lockf \
	logger \
	login \
	logname \
	look \
	lorder \
	lsvfs \
	lt \
	lzmainfo \
	m4 \
	mail \
	mandoc \
	mesg \
	mkcsmapper \
	mkdep \
	mkesdb \
	mkfifo \
	mklocale \
	mkstr \
	mktemp \
	monitor \
	msgs \
	mt \
	ncal \
	netstat \
	newgrp \
	newkey \
	nfsstat \
	nice \
	nl \
	nohup \
	objformat \
	opieinfo \
	opiekey \
	opiepasswd \
	pagesize \
	passwd \
	paste \
	patch \
	pathchk \
	pctrack \
	perror \
	pkg_radd \
	pkg_search \
	pkill \
	pr \
	printenv \
	printf \
	pwait \
	quota \
	rdist \
	relpath \
	renice \
	rev \
	rfcomm_sppd \
	rlogin \
	rpcgen \
	rpcinfo \
	rs \
	rsh \
	rup \
	ruptime \
	rusers \
	rwall \
	rwho \
	script \
	sdpquery \
	sed \
	seq \
	shar \
	shlock \
	showmount \
	sockstat \
	soelim \
	sort \
	split \
	stat \
	su \
	symorder \
	systat \
	tabs \
	tail \
	talk \
	tar \
	tcopy \
	tee \
	telnet \
	tftp \
	time \
	tic \
	tip \
	top \
	touch \
	tput \
	tr \
	true \
	truncate \
	truss \
	tset \
	tsort \
	tty \
	ul \
	uname \
	undo \
	unexpand \
	unifdef \
	uniq \
	units \
	unvis \
	${_usbhidctl} \
	users \
	uudecode \
	uuencode \
	vgrind \
	vi \
	vis \
	vknet \
	vmstat \
	w \
	wall \
	wc \
	what \
	whereis \
	which \
	who \
	whois \
	window \
	wmake \
	write \
	xargs \
	xinstall \
	xstr \
	xz \
	xzdec \
	yacc \
	yes \
	ypcat \
	ypmatch \
	ypwhich

.if defined(WANT_HESIOD)
SUBDIR+=hesinfo
.endif

.if defined(WANT_USB4BSD)
_usbhidctl=	u4bhidctl
.else
_usbhidctl=	usbhidctl
.endif

.if !defined(NO_OPENSSL)
SUBDIR+=bc \
	dc
.endif


.if !defined(NO_SENDMAIL)
SUBDIR+=vacation 
.endif

.if ${MACHINE_ARCH} == "i386"
# Things that don't compile on x86_64 or are i386 specific:
SUBDIR+=ncplist \
	ncplogin \
	smbutil
.endif

.include <bsd.subdir.mk>
