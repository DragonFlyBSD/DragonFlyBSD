TINFODIR=	${SHAREDIR}/terminfo
MISCDIR=	${.CURDIR}/../../contrib/ncurses/misc
TSOURCE=	${MISCDIR}/terminfo.src
TICBOOTSTRAP=	${.OBJDIR}/../../lib/libncurses/tic_next/tic_next.nx

TERMINFO_ENTRIES!=	LC_ALL=C awk -f ${.CURDIR}/entries.awk ${TSOURCE}
TINFO_SUBDIRS=		${TERMINFO_ENTRIES:H:O:u}

FILESGROUPS=	${TINFO_SUBDIRS:S/^/TINFO_/}
.for dir in ${TINFO_SUBDIRS}
TINFO_${dir}=		${TERMINFO_ENTRIES:M${dir}/*:S|^|terminfo/|}
TINFO_${dir}DIR=	${TINFODIR}/${dir}
.endfor

# Allow to override to reduce breakage surface (just in case)
.if exists(${TICBOOTSTRAP})
TIC?=	${TICBOOTSTRAP}
.else
TIC?=	tic
.endif

run_tic.sh: ${MISCDIR}/run_tic.in
	sed -e 's;@PROG_EXT@;;g' \
	    -e 's;@DESTDIR@;;g' \
	    -e 's;@TERMINFO@;${.OBJDIR}/terminfo;g' \
	    -e 's;@TERMINFO_SRC@;${TSOURCE};g' \
	    -e 's;@TIC@;${TIC};g' \
	    -e 's;@NCURSES_EXT_FUNCS@;1;g' \
	    -e 's;@LN_S@;ln -s -f;g' \
	    -e 's;@prefix@;/usr;g' \
	    -e 's;@exec_prefix@;/usr;g' \
	    -e 's;@bindir@;/usr/bin;g' \
	    -e 's;@datadir@;/usr/share;g' \
	    -e 's;@top_srcdir@;..;g' \
	    -e 's;@srcdir@;.;g' \
	    < ${.ALLSRC} > ${.TARGET}

terminfo: run_tic.sh ${TSOURCE}
	DESTDIR= sh run_tic.sh

CLEANFILES+=	run_tic.sh
CLEANDIRS+=	terminfo

all buildfiles: terminfo

.include <bsd.prog.mk>
