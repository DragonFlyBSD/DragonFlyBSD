# $DragonFly: src/gnu/usr.bin/gdb/libgdb/Makefile,v 1.7 2008/02/04 01:38:11 corecode Exp $

.include "../Makefile.inc"

LIB=	gdb
INTERNALLIB=	# yes

CONTRIBDIR=	${BASEDIR}/gdb
.PATH:	${CONTRIBDIR} ${CONTRIBDIR}/cli ${CONTRIBDIR}/mi ${CONTRIBDIR}/signals
.PATH:	${CONTRIBDIR}/tui

CFLAGS+= -I${.OBJDIR} -I${.CURDIR} -I${BASEDIR}/include -I${BASEDIR}/bfd
CFLAGS+= -I${CONTRIBDIR} -I${CONTRIBDIR}/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${BASEDIR} 
CFLAGS+= -DTARGET_SYSTEM_ROOT='"/"' -DBINDIR='"/usr/bin"'
CFLAGS+= -DMACHINE_ARCH='"${MACHINE_ARCH}"'

CLEANFILES+=	observer.h observer.inc gdb_stdint.h

SRCS+=	observer.h gdb_stdint.h

# The order of these sources is important,
# because init.c below needs to obey a certain ordering
# of function calls.

# COMMON_OBS
SRCS+=	version.c \
	annotate.c \
	auxv.c \
	bfd-target.c \
	blockframe.c breakpoint.c findvar.c regcache.c \
	charset.c disasm.c dummy-frame.c \
	source.c value.c eval.c valops.c valarith.c valprint.c printcmd.c \
	block.c symtab.c symfile.c symmisc.c linespec.c dictionary.c \
	infcall.c \
	infcmd.c infrun.c \
	expprint.c environ.c stack.c thread.c \
	exceptions.c \
	inf-child.c \
	interps.c \
	main.c \
	macrotab.c macrocmd.c macroexp.c macroscope.c \
	event-loop.c event-top.c inf-loop.c completer.c \
	gdbarch.c arch-utils.c gdbtypes.c osabi.c copying.c \
	memattr.c mem-break.c target.c parse.c language.c buildsym.c \
	std-regs.c \
	signals.c \
	gdb-events.c \
	exec.c bcache.c objfiles.c observer.c minsyms.c maint.c demangle.c \
	dbxread.c coffread.c coff-pe-read.c \
	dwarf2read.c mipsread.c stabsread.c corefile.c \
	dwarf2expr.c dwarf2loc.c dwarf2-frame.c \
	ada-lang.c c-lang.c f-lang.c objc-lang.c \
	ui-out.c cli-out.c \
	varobj.c vec.c wrapper.c \
	jv-lang.c jv-valprint.c jv-typeprint.c \
	m2-lang.c p-lang.c p-typeprint.c p-valprint.c \
	scm-exp.c scm-lang.c scm-valprint.c \
	sentinel-frame.c \
	complaints.c typeprint.c \
	ada-typeprint.c c-typeprint.c f-typeprint.c m2-typeprint.c \
	ada-valprint.c c-valprint.c cp-valprint.c f-valprint.c m2-valprint.c \
	serial.c mdebugread.c top.c utils.c \
	ui-file.c \
	user-regs.c \
	frame.c frame-unwind.c doublest.c \
	frame-base.c \
	gnu-v2-abi.c gnu-v3-abi.c cp-abi.c cp-support.c \
	cp-namespace.c \
	reggroups.c regset.c \
	trad-frame.c \
	tramp-frame.c \
	solib.c solib-null.c \
	prologue-value.c memory-map.c \
	target-descriptions.c target-memory.c xml-tdesc.c

# TSOBS
SRCS+=	inflow.c

# SER_HARDWIRE
SRCS+=	ser-base.c ser-unix.c ser-pipe.c ser-tcp.c

# TDEPFILES (part 1, the other is in Makefile.ARCH)
SRCS+=	bsd-uthread.c corelow.c solib-svr4.c

# NATDEPFILES (part 1, the other is in Makefile.ARCH)
SRCS+=	fork-child.c inf-ptrace.c gcore.c fbsd-nat.c

# REMOTE_OBS
SRCS+=	remote.c dcache.c tracepoint.c ax-general.c ax-gdb.c remote-fileio.c

# SUBDIR_CLI_OBS
SRCS+=  cli-dump.c \
	cli-decode.c cli-script.c cli-cmds.c cli-setshow.c cli-utils.c \
	cli-logging.c \
	cli-interp.c

# SUBDIR_MI_OBS
SRCS+=	mi-out.c mi-console.c \
	mi-cmds.c mi-cmd-env.c mi-cmd-var.c mi-cmd-break.c mi-cmd-stack.c \
	mi-cmd-file.c mi-cmd-disas.c mi-symbol-cmds.c \
	mi-interp.c \
	mi-main.c mi-parse.c mi-getopt.c mi-common.c

# CONFIG_OBS
SRCS+=	elfread.c posix-hdep.c

# SUBDIR_TUI_OBS
SRCS+=	tui-command.c \
	tui-data.c \
	tui-disasm.c \
	tui-file.c tui.c \
	tui-hooks.c \
	tui-interp.c \
	tui-io.c \
	tui-layout.c \
	tui-out.c \
	tui-regs.c \
	tui-source.c \
	tui-stack.c \
	tui-win.c \
	tui-windata.c \
	tui-wingeneral.c \
	tui-winsource.c

# YYOBJ
SRCS+=	c-exp.y \
	cp-name-parser.y \
	objc-exp.y \
	ada-exp.y \
	jv-exp.y \
	f-exp.y m2-exp.y p-exp.y

SRCS+=	init.c

# kernel-debugger
#SRCS+=	freebsd-uthread.c
#SRCS+=	kthr.c trgt.c trgt_${MACHINE_ARCH}.c

DPADD=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	../libbfd/libbfd.a ../libopcodes/libopcodes.a ../libiberty/libiberty.a
LDADD=	-ledit -lkvm -ltermcap -lm
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

CLEANFILES+=	init.c

# observer.inc can't be included in SRCS, it would duplicate the entry in OBJS
observer.h: observer.sh observer.inc doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh h ${CONTRIBDIR}/doc/observer.texi ${.TARGET}
observer.inc: observer.sh doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh inc ${CONTRIBDIR}/doc/observer.texi ${.TARGET}

gdb_stdint.h:
	touch ${.TARGET}

.include <bsd.lib.mk>
