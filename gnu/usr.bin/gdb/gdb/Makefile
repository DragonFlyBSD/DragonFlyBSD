# $DragonFly: src/gnu/usr.bin/gdb/gdb/Makefile,v 1.5 2004/11/17 12:27:59 joerg Exp $

.include "../Makefile.inc"

PROG=	gdb

SUBDIR=	doc

CONTRIBDIR=	${BASEDIR}/gdb
.PATH:	${CONTRIBDIR} ${CONTRIBDIR}/cli ${CONTRIBDIR}/mi ${CONTRIBDIR}/signals
.PATH:	${CONTRIBDIR}/tui

CFLAGS+= -I${.OBJDIR} -I${.CURDIR} -I${BASEDIR}/include -I${BASEDIR}/bfd
CFLAGS+= -I${CONTRIBDIR} -I${CONTRIBDIR}/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${BASEDIR} 

.if defined(GDB_TUI)
CFLAGS+= -DTUI=1
SRCS=	tui-main.c
.else
SRCS=	gdb.c
.endif

CLEANFILES+=	nm.h observer.h observer.inc tm.h xm.h

SRCS+=	nm.h observer.h tm.h xm.h

# libgdb
SRCS+=	annotate.c arch-utils.c auxv.c ax-gdb.c ax-general.c bcache.c
SRCS+=	bfd-target.c block.c blockframe.c breakpoint.c bsd-kvm.c buildsym.c
SRCS+=	c-exp.y c-lang.c c-typeprint.c c-valprint.c charset.c cli-cmds.c
SRCS+=	cli-decode.c cli-dump.c cli-interp.c cli-logging.c cli-out.c
SRCS+=	cli-script.c cli-setshow.c cli-utils.c coff-pe-read.c coffread.c
SRCS+=	complaints.c completer.c copying.c corefile.c corelow.c cp-abi.c
SRCS+=	cp-namespace.c cp-support.c cp-valprint.c dbxread.c dcache.c demangle.c
SRCS+=	dictionary.c disasm.c doublest.c dummy-frame.c dwarf2-frame.c
SRCS+=	dwarf2expr.c dwarf2loc.c dwarf2read.c dwarfread.c elfread.c environ.c
SRCS+=	eval.c event-loop.c event-top.c exec.c expprint.c f-exp.y f-lang.c
SRCS+=	f-typeprint.c f-valprint.c fbsd-proc.c findvar.c fork-child.c
SRCS+=	frame-base.c frame-unwind.c frame.c gcore.c gdb-events.c gdbarch.c
SRCS+=	gdbtypes.c gnu-v2-abi.c gnu-v3-abi.c hpacc-abi.c inf-loop.c infcall.c
SRCS+=	infcmd.c inflow.c infptrace.c infrun.c inftarg.c init.c interps.c
SRCS+=	jv-exp.y jv-lang.c jv-typeprint.c jv-valprint.c kod-cisco.c kod.c
SRCS+=	language.c linespec.c m2-exp.y m2-lang.c m2-typeprint.c m2-valprint.c
SRCS+=	macrocmd.c macroexp.c macroscope.c macrotab.c main.c maint.c
SRCS+=	mdebugread.c mem-break.c memattr.c mi-cmd-break.c mi-cmd-disas.c
SRCS+=	mi-cmd-env.c mi-cmd-file.c mi-cmd-stack.c mi-cmd-var.c mi-cmds.c
SRCS+=	mi-console.c mi-getopt.c mi-interp.c mi-main.c mi-out.c
SRCS+=	mi-parse.c mi-symbol-cmds.c minsyms.c mipsread.c nlmread.c objc-exp.y
SRCS+=	objc-lang.c objfiles.c observer.c osabi.c p-exp.y p-lang.c
SRCS+=	p-typeprint.c p-valprint.c parse.c printcmd.c regcache.c reggroups.c
SRCS+=	regset.c remote-fileio.c remote-utils.c remote.c scm-exp.c scm-lang.c
SRCS+=	scm-valprint.c sentinel-frame.c ser-pipe.c ser-tcp.c ser-unix.c
SRCS+=	serial.c signals.c solib-legacy.c solib-svr4.c solib.c source.c
SRCS+=	stabsread.c
SRCS+=	stack.c std-regs.c symfile.c symmisc.c symtab.c target.c thread.c
SRCS+=	top.c tracepoint.c trad-frame.c tramp-frame.c typeprint.c
SRCS+=	ui-file.c ui-out.c user-regs.c utils.c valarith.c valops.c valprint.c
SRCS+=	values.c varobj.c version.c wrapper.c
# tui specific
.if defined(GDB_TUI)
SRCS+=	tui.c tui-command.c tui-data.c tui-disasm.c tui-file.c tui-hooks.c
SRCS+=	tui-interp.c tui-io.c tui-layout.c tui-out.c tui-regs.c tui-source.c
SRCS+=	tui-stack.c tui-win.c tui-windata.c tui-wingeneral.c tui-winsource.c
.endif
# kernel-debugger
PATCHES!=	echo ${.CURDIR}/patches/*.patch
SRCS+=	freebsd-uthread.c kvm-fbsd.c kvm-fbsd.h ${PATCHES}

DPADD=	${LIBREADLINE} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	../libbfd/libbfd.a ../libopcodes/libopcodes.a ../libiberty/libiberty.a
LDADD=	-lreadline -lkvm -ltermcap -lm
LDADD+=	-L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty

.include "Makefile.${MACHINE_ARCH}"

init.c: ${SRCS:Ninit.c} Makefile
	echo '#include "defs.h"' > ${.TARGET}
	echo '#include "call-cmds.h"' >> ${.TARGET}
	sed -n -e 's/^_initialize_\([a-z_0-9A-Z]*\).*/extern initialize_file_ftype _initialize_\1;/p' ${.ALLSRC:M*.c} >> ${.TARGET}
	echo 'void' >> ${.TARGET}
	echo 'initialize_all_files(void)' >> ${.TARGET}
	echo '{' >> ${.TARGET}
	sed -n -e 's/^_initialize_\([a-z_0-9A-Z]*\).*/  _initialize_\1 ();/p' ${.ALLSRC:M*.c} >> ${.TARGET}
	echo '}' >> ${.TARGET}

# observer.inc can't be included in SRCS, it would duplicate the entry in OBJS
observer.h: observer.sh observer.inc doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh h ${CONTRIBDIR}/doc/observer.texi ${.TARGET}
observer.inc: observer.sh doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh inc ${CONTRIBDIR}/doc/observer.texi ${.TARGET}

nm.h: nm-fbsd.h
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
