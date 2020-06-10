.include 	"../Makefile.inc"

LIB=		diffutils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth ${CONTRIBDIR}/unistr

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=	allocator.c \
	areadlink.c \
	argmatch.c \
	binary-io.c \
	bitrotate.c \
	c-ctype.c \
	c-stack.c \
	c-strcasecmp.c \
	c-strncasecmp.c \
	careadlinkat.c \
	cloexec.c \
	dirname.c \
	basename.c \
	dirname-lgpl.c \
	basename-lgpl.c \
	stripslash.c \
	exclude.c \
	exitfail.c \
	fd-hook.c \
	file-type.c \
	filenamecat.c \
	filenamecat-lgpl.c \
	freopen-safer.c \
	getprogname.c \
	gettime.c \
	hard-locale.c \
	hash.c \
	imaxtostr.c \
	inttostr.c \
	offtostr.c \
	uinttostr.c \
	umaxtostr.c \
	localcharset.c \
	malloca.c \
	mbchar.c \
	mbiter.c \
	mbscasecmp.c \
	mbslen.c \
	mbsstr.c \
	mbuiter.c \
	nstrftime.c \
	progname.c \
	propername.c \
	quotearg.c \
	sh-quote.c \
	sig-handler.c \
	stat-time.c \
	striconv.c \
	strnlen1.c \
	system-quote.c \
	tempname.c \
	timespec.c \
	trim.c \
	unistd.c \
	u8-mbtoucr.c \
	u8-uctomb.c \
	u8-uctomb-aux.c \
	width.c \
	version-etc.c \
	version-etc-fsf.c \
	wctype-h.c \
	xmalloc.c \
	xalloc-die.c \
	xfreopen.c \
	xreadlink.c \
	xsize.c \
	xstriconv.c \
	xstrndup.c \
	xstrtol.c \
	xstrtoul.c \
	xstrtol-error.c \
	xstrtoumax.c \
	xvasprintf.c \
	xasprintf.c \
	cmpbuf.c \
	prepargs.c \
	asnprintf.c \
	dup2.c \
	error.c \
	fcntl.c \
	fnmatch.c \
	freopen.c \
	getopt.c \
	getopt1.c \
	localtime-buffer.c \
	mktime.c \
	printf-args.c \
	printf-parse.c \
	rawmemchr.c \
	regex.c \
	strerror.c \
	strerror-override.c \
	time_rz.c \
	timegm.c \
	vasnprintf.c

# sources that produces empty objects
#SRCS+=	\
	fd-hook.c \
	unistd.c \
	wctype-h.c

.include <bsd.lib.mk>
