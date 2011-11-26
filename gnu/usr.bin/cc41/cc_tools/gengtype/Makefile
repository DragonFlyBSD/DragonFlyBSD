# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/gengtype/Makefile,v 1.2 2007/08/25 15:29:29 corecode Exp $

.include "../Makefile.inc"

PROG=	gengtype
SRCS=	gengtype.c gtyp-gen.h gengtype-lex.l gengtype-yacc.y
SRCS+=	${ERRORS_SRCS}

CLEANFILES+=	gtyp-gen.h

gtyp-gen.h: Makefile ../../Makefile.langs ${GTFILES}
	echo "/* This file is machine generated.  Do not edit.  */" > gtyp-gen.h
	echo "static const char *const srcdir = "  >> gtyp-gen.h
	echo "\"$(GTFILES_SRCDIR)\"" >> gtyp-gen.h
	echo ";" >> gtyp-gen.h
	echo "static const char *const lang_files[] = {" >> gtyp-gen.h
.for gtyp in ${GTFILES_FILES_FILES}
	echo "\"${gtyp}\", " >> gtyp-gen.h
.endfor
	echo "NULL};" >> gtyp-gen.h
	echo "static const char *const langs_for_lang_files[] = {" >> gtyp-gen.h
.for gtyp in ${GTFILES_FILES_LANGS}
	echo "\"${gtyp}\", " >> gtyp-gen.h
.endfor
	echo "NULL};" >> gtyp-gen.h
	echo "static const char *const all_files[] = {" >> gtyp-gen.h
.for gtyp in ${GTFILES}
	echo "\"${gtyp}\", " >> gtyp-gen.h
.endfor
	echo " NULL};" >> gtyp-gen.h
	echo "static const char *const lang_dir_names[] = { \"c\", " >> gtyp-gen.h
.for gtyp in ${GTFILES_LANG_DIR_NAMES}
	echo "\"${gtyp}\", " >> gtyp-gen.h
.endfor
	echo "NULL};" >> gtyp-gen.h

all: ${TOOLGENDIR}/gtype-stamp

${TOOLGENDIR}/gtype-stamp: ${PROG}.nx
	cd ${TOOLGENDIR} && ${.OBJDIR}/${PROG}.nx
	touch ${.TARGET}

.include <bsd.hostprog.mk>
