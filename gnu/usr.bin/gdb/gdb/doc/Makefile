# $DragonFly: src/gnu/usr.bin/gdb/gdb/doc/Makefile,v 1.1 2004/10/24 19:05:39 joerg Exp $

RELATIVE=	../

.include "../../Makefile.inc"

SRCDIR=		${BASEDIR}/gdb/doc
RLUSER=		${.CURDIR}/../../../../../contrib/readline-5.0/doc/rluser.texi
HSUSER=		${.CURDIR}/../../../../../contrib/readline-5.0/doc/hsuser.texi

.PATH:		${SRCDIR}

INFO=	gdb gdbint stabs annotate

INFOSECTION=		"Programming & development tools."
INFOENTRY_gdb=		"* GDB.					The GNU debugger."
INFOENTRY_gdbint=	"* GDB-internals.			The GNU debugger's internals."
INFOENTRY_stabs=	"* Stabs.				The "stabs" debugging information format."
INFOENTRY_annotate=	"* GDB annotation.			Annotations for the GNU debugger (GDB)."

CLEANFILES=	gdb-cfg.texi

gdb.info: gdb.texinfo fdl.texi gpl.texi agentexpr.texi gdb-cfg.texi GDBvn.texi rluser.texinfo inc-hist.texinfo

gdb-cfg.texi: ${SRCDIR}/all-cfg.texi
	cp ${SRCDIR}/all-cfg.texi ${.TARGET}

gdbint.info: gdbint.texinfo fdl.texi gpl.texi gdb-cfg.texi GDBvn.texi

stabs.info: stabs.texinfo fdl.texi

annotate.info: annotate.texinfo fdl.texi gdb-cfg.texi

.include <bsd.info.mk>
