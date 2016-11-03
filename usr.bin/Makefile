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
	bc \
	biff \
	bmake \
	brandelf \
	bsdcat \
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
	chpass \
	cksum \
	cmp \
	col \
	colcrt \
	colrm \
	column \
	comm \
	compress \
	cpio \
	crunch \
	csplit \
	ctags \
	cut \
	dfregress \
	dirname \
	du \
	ee \
	enigma \
	env \
	evtranalyze \
	expand \
	false \
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
	localedef \
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
	mkstr \
	mktemp \
	monitor \
	msgs \
	mt \
	nc \
	ncal \
	netstat \
	newgrp \
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
	smbutil \
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
	unzip \
	usbhidaction \
	u4bhidctl \
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

# Uses lib[re]ssl but can build without it
SUBDIR+=ftp \
	telnet

# Requires lib[re]ssl
#
.if !defined(NO_CRYPT)
SUBDIR+=chkey \
	dc \
	drill \
	fetch \
	newkey \
	bdes \
	openssl \
	scp \
	sftp \
	ssh \
	ssh-add \
	ssh-agent \
	ssh-keygen \
	ssh-keyscan
.endif

.include <bsd.subdir.mk>
