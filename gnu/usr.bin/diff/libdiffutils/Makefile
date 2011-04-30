.include 	"../Makefile.inc"

LIB=		diffutils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=	c-stack.c \
	c-ctype.c \
	c-strcasecmp.c \
	c-strncasecmp.c \
	exitfail.c \
	localcharset.c \
	malloca.c \
	mbchar.c \
	mbscasecmp.c \
	mbslen.c \
	mbsstr.c \
	progname.c \
	propername.c \
	sh-quote.c \
	striconv.c \
	strnlen1.c \
	trim.c \
	width.c \
	version-etc.c \
	version-etc-fsf.c \
	xalloc-die.c \
	xfreopen.c \
	xstriconv.c \
	xstrndup.c \
	xstrtoumax.c \
	cmpbuf.c \
	prepargs.c \
	basename.c \
	basename-lgpl.c \
	dirname.c \
	dirname-lgpl.c \
	dup2.c \
	error.c \
	exclude.c \
	fcntl.c \
	file-type.c \
	fnmatch.c \
	freopen-safer.c \
	getopt.c \
	getopt1.c \
	gettime.c \
	hard-locale.c \
	hash.c \
	imaxtostr.c \
	mktime.c \
	offtostr.c \
	quote.c \
	quotearg.c \
	regex.c \
	strftime.c \
	stripslash.c \
	tempname.c \
	timegm.c \
	uinttostr.c \
	umaxtostr.c \
	wcwidth.c \
	xmalloc.c \
	xstrtol.c \
	xstrtol-error.c \
	xstrtoul.c

.include <bsd.lib.mk>
