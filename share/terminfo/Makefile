.include "Makefile.entries"

NOMAN=

CLEANFILES+=	run_tic.sed run_tic.sh terminfo.tmp
CLEANDIRS+=	terminfo

TINFODIR=	${SHAREDIR}/terminfo
MISCDIR=	${.CURDIR}/../../contrib/ncurses/misc
PSOURCE=	${.CURDIR}/../../contrib/ncurses/progs
TSOURCE=	${MISCDIR}/terminfo.src
CFLAGS+=	-I${PSOURCE} -I${.CURDIR}/../../lib/libncurses/include

all: terminfo

run_tic.sed:
	WHICH_XTERM=xterm-new \
	ticdir=${FILESDIR}/terminfo \
	sh ${MISCDIR}/gen_edit.sh > ${.TARGET}

run_tic.sh:
	cat ${MISCDIR}/run_tic.in | sed \
	  -e 's;@PROG_EXT@;;g' \
	  -e 's;@DESTDIR@;;g' \
	  -e 's;@TERMINFO@;${.CURDIR}/terminfo;g' \
	  -e 's;@TERMINFO_SRC@;${TSOURCE};g' \
	  -e 's;@TIC_PATH@;tic;g' \
	  -e 's;@NCURSES_EXT_FUNCS@;1;g' \
	  -e 's;@LN_S@;ln -s -f;g' \
	  -e 's;@prefix@;/usr;g' \
	  -e 's;@exec_prefix@;/usr;g' \
	  -e 's;@bindir@;/usr/bin;g' \
	  -e 's;@datadir@;/usr/share;g' \
	  -e 's;@top_srcdir@;..;g' \
	  -e 's;@srcdir@;.;g' \
	> ${.TARGET}


terminfo.tmp: run_tic.sed ${TSOURCE}
	sed -f run_tic.sed ${TSOURCE} > ${.TARGET}

terminfo: run_tic.sh terminfo.tmp
	sh run_tic.sh

install:
# Directories aren't created on the fly; handled by mtree/BSD.usr.dist
#	for ITEM in ${LEVEL1}; do \
#		${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
#		${DESTDIR}${TINFODIR}/$${ITEM}; \
#	done
	for ITEM in ${ENTRY}; do \
		${INSTALL} -m 644 -o ${BINOWN} -g ${BINGRP}  \
		terminfo/$${ITEM} ${DESTDIR}${TINFODIR}/$${ITEM}; \
	done

.include <bsd.prog.mk>
