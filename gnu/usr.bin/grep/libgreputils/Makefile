.include "../Makefile.inc"

LIB=		greputils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth ${CONTRIBDIR}/unistr \
		${CONTRIBDIR}/glthread

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		argmatch.c \
		binary-io.c \
		bitrotate.c \
		c-ctype.c \
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
		fd-hook.c \
		filenamecat-lgpl.c \
		hash.c \
		i-ring.c \
		localcharset.c \
		lock.c \
		malloca.c \
		mbchar.c \
		mbiter.c \
		mbscasecmp.c \
		mbslen.c \
		mbsstr.c \
		mbuiter.c \
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
		threadlib.c \
		trim.c \
		unistd.c \
		dup-safer.c \
		fd-safer.c \
		pipe-safer.c \
		u8-mbtoucr.c \
		u8-uctomb.c \
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
		strerror.c \
		strerror-override.c \
		wcwidth.c

.include <bsd.lib.mk>
