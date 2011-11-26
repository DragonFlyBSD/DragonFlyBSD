RELATIVE=	../

.include "../../Makefile.inc"

SRCDIR=		${BASEDIR}/gdb/doc
READLINEDIR=	${BASEDIR}/readline/doc

.PATH:		${SRCDIR} ${READLINEDIR}
MAKEINFOFLAGS+=	-I ${READLINEDIR}

INFO=	gdb gdbint stabs annotate

INFOSECTION=		"Programming & development tools."
INFOENTRY_gdb=		"* GDB.					The GNU debugger."
INFOENTRY_gdbint=	"* GDB-internals.			The GNU debugger's internals."
INFOENTRY_stabs=	"* Stabs.				The "stabs" debugging information format."
INFOENTRY_annotate=	"* GDB annotation.			Annotations for the GNU debugger (GDB)."

CLEANFILES=	gdb-cfg.texi

gdb.info: gdb.texinfo fdl.texi gpl.texi agentexpr.texi \
	rluser.texi inc-hist.texinfo \
	gdb-cfg.texi GDBvn.texi

gdb-cfg.texi: ${SRCDIR}/all-cfg.texi
	cp ${SRCDIR}/all-cfg.texi ${.TARGET}

gdbint.info: gdbint.texinfo fdl.texi observer.texi gdb-cfg.texi GDBvn.texi

stabs.info: stabs.texinfo fdl.texi

annotate.info: annotate.texinfo fdl.texi gdb-cfg.texi GDBvn.texi

.include <bsd.info.mk>
