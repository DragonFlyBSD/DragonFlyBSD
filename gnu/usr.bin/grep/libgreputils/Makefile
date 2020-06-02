.include "../Makefile.inc"

LIB=		greputils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth ${CONTRIBDIR}/unistr \
		${CONTRIBDIR}/glthread

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.endif

SRCS=		argmatch.c \
		basename-lgpl.c \
		c-stack.c \
		c-strcasecmp.c \
		c-strncasecmp.c \
		chdir-long.c \
		cloexec.c \
		close-stream.c \
		closeout.c \
		colorize-posix.c \
		creat-safer.c \
		cycle-check.c \
		dfa.c \
		dirname-lgpl.c \
		dup-safer-flag.c \
		dup-safer.c \
		dup2.c \
		error.c \
		exclude.c \
		exitfail.c \
		fcntl.c \
		fd-safer-flag.c \
		fd-safer.c \
		filenamecat-lgpl.c \
		fts.c \
		getopt.c \
		getopt1.c \
		getprogname.c \
		hard-locale.c \
		hash.c \
		i-ring.c \
		localcharset.c \
		localeinfo.c \
		lock.c \
		malloca.c \
		mbchar.c \
		mbscasecmp.c \
		mbslen.c \
		mbsstr.c \
		memchr2.c \
		obstack.c \
		open-safer.c \
		openat-die.c \
		openat-proc.c \
		openat-safer.c \
		opendirat.c \
		pipe-safer.c \
		propername.c \
		quotearg.c \
		regex.c \
		safe-read.c \
		save-cwd.c \
		setlocale_null.c \
		setlocale-lock.c \
		sig-handler.c \
		stat-time.c \
		strerror-override.c \
		strerror.c \
		striconv.c \
		stripslash.c \
		strnlen1.c \
		strstr.c \
		trim.c \
		u8-mbtoucr.c \
		u8-uctomb-aux.c \
		width.c \
		version-etc-fsf.c \
		version-etc.c \
		wctype-h.c \
		xalloc-die.c \
		xbinary-io.c \
		xmalloc.c \
		xstriconv.c \
		xstrtoimax.c \
		xstrtol.c \
		xstrtoul.c

# sources that produces empty objects
#SRCS+=		binary-io.c \
		bitrotate.c \
		c-ctype.c \
		fd-hook.c \
		mbiter.c \
		mbuiter.c \
		threadlib.c \
		unistd.c \
		u8-uctomb.c

.include <bsd.lib.mk>
