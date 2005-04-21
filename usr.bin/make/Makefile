#	@(#)Makefile	5.2 (Berkeley) 12/28/90
# $FreeBSD: src/usr.bin/make/Makefile,v 1.13.2.1 2001/05/25 08:33:40 sobomax Exp $
# $DragonFly: src/usr.bin/make/Makefile,v 1.20 2005/04/21 23:03:13 okumoto Exp $

PROG=	make
CFLAGS+=-I${.CURDIR}
SRCS=	arch.c buf.c compat.c cond.c dir.c directive_hash.c for.c hash.c \
	job.c lst.c main.c make.c parse.c str.c suff.c targ.c util.c var.c

WARNS?=	6
NOSHARED?=	YES

CFLAGS+=-DMAKE_VERSION=\"5200408120\"

# There is no obvious performance improvement currently.
# CFLAGS+=-DUSE_KQUEUE

main.o: ${MAKEFILE}

# Directive table. We use a hash table. This hash table has been
# generated with mph which can be found on the usual GNU mirrors.
# If you change the directives (adding, deleting, reordering) you
# need to create a new table and hash function (directive_hash).
#
# The following changes have been made to the generated code:
#
#	o prefix the names of the g, T0 and T1 arrays with 'directive_'.
#
#	o make the type of the tables 'const [un]signed char' (if you change
#	  anything make sure that the numbers fit into a char).
#
#	o make the hash function use the length for termination,
#	  not the trailing '\0', via the -l flag in emitc and some editing.

LOCALBASE ?= /usr/local
MPH	?= ${LOCALBASE}/bin/mph
EMITC	?= ${LOCALBASE}/bin/emitc

.PRECIOUS:
hash:
	echo '/*'				> directive_hash.c
	echo ' * DO NOT EDIT'			>> directive_hash.c
	echo ' * $$DragonFly: src/usr.bin/make/Makefile,v 1.20 2005/04/21 23:03:13 okumoto Exp $$'			>> directive_hash.c
	echo -n ' * auto-generated from '	>> directive_hash.c
	sed -nEe '/\$$(DragonFly)|(FreeBSD)/s/^.*\$$(.*)\$$.*$$/\1/p' \
		${.CURDIR}/parse.c >>directive_hash.c
	echo ' * DO NOT EDIT'			>> directive_hash.c
	echo ' */'				>> directive_hash.c
	echo '#include <sys/types.h>'		>> directive_hash.c
	echo ''					>> directive_hash.c
	echo '#include "directive_hash.h"'	>> directive_hash.c
	echo ''					>> directive_hash.c
	cat ${.CURDIR}/parse.c | sed \
	    -e '1,/DIRECTIVES-START-TAG/d' -e '/DIRECTIVES-END-TAG/,$$d' \
	    -e 's/^[^"]*"\([^"]*\)".*$$/\1/' |\
	    ${MPH} -d2 -m1 | ${EMITC} -l -s |\
	    sed \
	    -e 's/^static int g\[\]/static const signed char directive_g[]/' \
	    -e 's/^static int T0\[\]/static const u_char directive_T0[]/' \
	    -e 's/^static int T1\[\]/static const u_char directive_T1[]/' \
	    -e '/^#define uchar unsigned char/d' \
	    -e 's/uchar/u_char/g' \
	    -e 's/^hash(/directive_hash(/' \
	    -e 's/; \*kp;/; kp < key + len;/' \
	    -e 's/int len)/size_t len)/' \
	    -e 's/= T0\[/= directive_T0\[/' \
	    -e 's/= T1\[/= directive_T1\[/' \
	    -e 's/g\[f/directive_g[f/g' \
	    >> directive_hash.c

# Set the shell which make(1) uses.  Bourne is the default, but a decent
# Korn shell works fine, and much faster.  Using the C shell for this
# will almost certainly break everything, but it's Unix tradition to
# allow you to shoot yourself in the foot if you want to :-)

MAKE_SHELL?=	sh
.if ${MAKE_SHELL} == "csh"
CFLAGS+=	-DDEFSHELL=0
.elif ${MAKE_SHELL} == "sh"
CFLAGS+=	-DDEFSHELL=1
.elif ${MAKE_SHELL} == "ksh"
CFLAGS+=	-DDEFSHELL=2
.else
.error "MAKE_SHELL must be set to one of \"csh\", \"sh\" or \"ksh\"."
.endif

.include <bsd.prog.mk>
