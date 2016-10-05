.include "../Makefile.inc"

LIB=		greputils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth ${CONTRIBDIR}/unistr \
		${CONTRIBDIR}/glthread

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		argmatch.c \
		c-strcasecmp.c \
		c-strncasecmp.c \
		cloexec.c \
		close-stream.c \
		closeout.c \
		cycle-check.c \
		opendir-safer.c \
		dirname-lgpl.c \
		basename-lgpl.c \
		stripslash.c \
		exclude.c \
		exitfail.c \
		creat-safer.c \
		open-safer.c \
		fcntl.c \
		filenamecat-lgpl.c \
		hash.c \
		i-ring.c \
		localcharset.c \
		lock.c \
		malloca.c \
		mbchar.c \
		mbscasecmp.c \
		mbslen.c \
		mbsstr.c \
		memchr2.c \
		openat-die.c \
		openat-safer.c \
		progname.c \
		propername.c \
		quotearg.c \
		safe-read.c \
		save-cwd.c \
		striconv.c \
		strnlen1.c \
		trim.c \
		dup-safer.c \
		dup2.c \
		fd-safer.c \
		pipe-safer.c \
		u8-mbtoucr.c \
		u8-uctomb-aux.c \
		width.c \
		version-etc.c \
		version-etc-fsf.c \
		xmalloc.c \
		xalloc-die.c \
		xstriconv.c \
		xstrtoimax.c \
		xstrtol.c \
		xstrtoul.c \
		xstrtol-error.c \
		colorize-posix.c \
		chdir-long.c \
		error.c \
		fts.c \
		getopt.c \
		getopt1.c \
		obstack.c \
		openat-proc.c \
		regex.c \
		strstr.c \
		strerror.c \
		strerror-override.c

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
