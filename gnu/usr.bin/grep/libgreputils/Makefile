.include "../Makefile.inc"

LIB=		greputils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth ${CONTRIBDIR}/unistr

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		argmatch.c \
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
		hard-locale.c \
		hash.c \
		i-ring.c \
		localcharset.c \
		malloca.c \
		mbchar.c \
		mbscasecmp.c \
		mbslen.c \
		mbsstr.c \
		openat-die.c \
		openat-safer.c \
		progname.c \
		propername.c \
		quotearg.c \
		save-cwd.c \
		striconv.c \
		strnlen1.c \
		trim.c \
		dup-safer.c \
		fd-safer.c \
		u8-mbtoucr.c \
		u8-uctomb.c \
		u8-uctomb-aux.c \
		pipe-safer.c \
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
		colorize.c \
		chdir-long.c \
		dup2.c \
		error.c \
		fcntl.c \
		fts.c \
		getopt.c \
		getopt1.c \
		obstack.c \
		openat-proc.c \
		regex.c \
		strerror.c \
		strerror-override.c

.if defined(BOOTSTRAPPING)
DFVER!=		file -b /bin/ls | awk -F, '{split($$5,arr,"."); print arr[3]}'
.  if ${DFVER} < 503
SRCS+=		wcwidth.c
.  endif
.else
SRCS+=		wcwidth.c
.endif

.include <bsd.lib.mk>
