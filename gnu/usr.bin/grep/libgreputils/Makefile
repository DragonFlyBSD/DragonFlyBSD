.include "../Makefile.inc"

LIB=		greputils
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/lib
.PATH:		${CONTRIBDIR} ${CONTRIBDIR}/uniwidth

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		c-ctype.c \
		c-strcasecmp.c \
		c-strncasecmp.c \
		exitfail.c \
		isdir.c \
		localcharset.c \
		malloca.c \
		mbchar.c \
		mbscasecmp.c \
		mbslen.c \
		mbsstr.c \
		progname.c \
		propername.c \
		striconv.c \
		strnlen1.c \
		trim.c \
		width.c \
		version-etc.c \
		version-etc-fsf.c \
		xalloc-die.c \
		xstriconv.c \
		xstrndup.c \
		xstrtoumax.c \
		savedir.c \
		argmatch.c \
		basename.c \
		basename-lgpl.c \
		close-stream.c \
		closeout.c \
		dirname.c \
		dirname-lgpl.c \
		error.c \
		exclude.c \
		getopt.c \
		getopt1.c \
		hard-locale.c \
		hash.c \
		mempcpy.c \
		obstack.c \
		quote.c \
		quotearg.c \
		regex.c \
		stripslash.c \
		wcwidth.c \
		xmalloc.c \
		xstrtol.c \
		xstrtol-error.c \
		xstrtoul.c

.include <bsd.lib.mk>
