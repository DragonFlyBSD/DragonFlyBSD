# $DragonFly: src/gnu/usr.bin/cc40/cc_tools/gengtype/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

.include "../Makefile.inc"
.include "../../Makefile.langs"

SPREPDIR=	${.CURDIR}/../../cc_prep

PROG=	gengtype
SRCS=	gengtype.c gtyp-gen.h gengtype-lex.c gengtype-yacc.c
SRCS+=	${ERRORS_SRCS}

CLEANFILES+=	gtyp-gen.h

# from gcc/Makefile
srcdir= ${GCCDIR}/gcc

host_xm_file_list=	${SPREPDIR}/dragonfly-native.h
host_xm_file_list+=	${GCCDIR}/include/ansidecl.h
HASHTAB_H   = ${GCCDIR}/include/hashtab.h
SPLAY_TREE_H= ${GCCDIR}/include/splay-tree.h
SYMTAB_H    = ${GCCDIR}/libcpp/include/symtab.h
CPP_ID_DATA_H=	${GCCDIR}/libcpp/include/line-map.h \
		${GCCDIR}/libcpp/include/cpplib.h \
		${GCCDIR}/libcpp/include/cpp-id-data.h

# XXX move this
# platform specific files
tm_file_list=	$(srcdir)/config/i386/i386.h $(srcdir)/config/i386/unix.h
tm_file_list+=	$(srcdir)/config/i386/att.h $(srcdir)/config/dbxelf.h
tm_file_list+=	$(srcdir)/config/elfos.h ${SPREPDIR}/config/dragonfly-spec.h
tm_file_list+=	${SPREPDIR}/config/dragonfly.h ${SPREPDIR}/config/i386/dragonfly.h
tm_file_list+=	$(srcdir)/defaults.h
out_file=	$(srcdir)/config/i386/i386.c

GTFILES_SRCDIR = $(srcdir)

gtyp-gen.h: Makefile ../../Makefile.langs
	echo "/* This file is machine generated.  Do not edit.  */" > gtyp-gen.h
	echo "static const char *const srcdir = "  >> gtyp-gen.h
	echo "\"$(GTFILES_SRCDIR)\"" >> gtyp-gen.h
	echo ";" >> gtyp-gen.h
	echo "static const char *const lang_files[] = {" >> gtyp-gen.h
	ll="$(GTFILES_FILES_FILES)"; \
	for f in $$ll; do \
	echo "\"$$f\", "; done >> gtyp-gen.h
	echo "NULL};" >> gtyp-gen.h
	echo "static const char *const langs_for_lang_files[] = {" >> gtyp-gen.h
	ff="$(GTFILES_FILES_LANGS)"; \
	for f in $$ff; do \
	echo "\"$$f\", " ; done  >> gtyp-gen.h
	echo "NULL};" >> gtyp-gen.h
	echo "static const char *const all_files[] = {" >> gtyp-gen.h
	gf="$(GTFILES)"; \
	for f in $$gf; do \
	echo "\"$$f\", "; done >> gtyp-gen.h
	echo " NULL};" >> gtyp-gen.h
	echo "static const char *const lang_dir_names[] = { \"c\", " >> gtyp-gen.h
	gf="$(GTFILES_LANG_DIR_NAMES)"; \
	for l in $$gf; do \
	echo "\"$$l\", "; done >> gtyp-gen.h
	echo "NULL};" >> gtyp-gen.h

.include <bsd.hostprog.mk>
