#	From: @(#)Makefile	8.3 (Berkeley) 1/7/94
# $FreeBSD: src/usr.bin/Makefile,v 1.144.2.17 2003/01/04 17:17:07 obrien Exp $
# $DragonFly: src/usr.bin/Makefile,v 1.26 2005/01/23 13:06:13 joerg Exp $

# XXX MISSING:		deroff diction graph learn plot
#			spell spline struct xsend
# XXX broken:		tconv
# XXX Use GNU versions: apropos diff grep ld man ptx uucp whatis
# Moved to secure: bdes
#
SUBDIR=	alias \
	apply \
	at \
	awk \
	asa \
	banner \
	basename \
	biff \
	brandelf \
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
	csplit \
	ctags \
	cut \
	dirname \
	du \
	ee \
	enigma \
	env \
	expand \
	false \
	fetch \
	file \
	file2c \
	find \
	finger \
	fmt \
	fold \
	from \
	fstat \
	fsync \
	ftp \
	gencat \
	getconf \
	getopt \
	gprof \
	gzip \
	head \
	hexdump \
	id \
	indent \
	ipcrm \
	ipcs \
	join \
	jot \
	kdump \
	kenv \
	key \
	keyinfo \
	keyinit \
	keylogin \
	keylogout \
	killall \
	ktrace \
	kzip \
	lam \
	last \
	lastcomm \
	ldd \
	leave \
	less \
	lessecho \
	lesskey \
	lex \
	limits \
	locate \
	lock \
	lockf \
	logger \
	login \
	logname \
	look \
	lorder \
	lsvfs \
	m4 \
	mail \
	make \
	mesg \
	minigzip \
	mkdep \
	mkfifo \
	mklocale \
	mkstr \
	mktemp \
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
	pkill \
	pr \
	printenv \
	printf \
	quota \
	rdist \
	renice \
	relpath \
	rev \
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
	sed \
	seq \
	shar \
	showmount \
	sockstat \
	soelim \
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
	unexpand \
	unifdef \
	uniq \
	units \
	unvis \
	usbhidctl \
	users \
	uudecode \
	uuencode \
	vgrind \
	vi \
	vis \
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
	xlint \
	xstr \
	yacc \
	yes \
	ypcat \
	ypmatch \
	ypwhich

.if !defined(NO_BIND)
SUBDIR+=dig \
	host \
	dnssec-keygen \
	dnssec-makekeyset \
	dnssec-signkey \
	dnssec-signzone
.endif

.if !defined(NO_OPENSSL)
SUBDIR+=bc \
	dc
.endif


.if !defined(NO_SENDMAIL)
SUBDIR+=vacation 
.endif

.if ${MACHINE_ARCH} == "i386"
# Things that don't compile on alpha or are aout specific:
SUBDIR+=doscmd \
	gcore \
	gprof4 \
	ncplist \
	ncplogin \
	sasc \
	smbutil
.endif

.if ${MACHINE_ARCH} == "alpha"
SUBDIR+=uac
.endif

.include <bsd.subdir.mk>
