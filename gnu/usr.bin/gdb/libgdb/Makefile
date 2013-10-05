.include "../Makefile.inc"

TARGET_ARCH?=	${MACHINE_ARCH}

LIB=		gdb
INTERNALLIB=	yes

CONTRIBDIR=	${BASEDIR}/gdb
.PATH:	${CONTRIBDIR} ${CONTRIBDIR}/cli ${CONTRIBDIR}/mi
.PATH:	${CONTRIBDIR}/common ${CONTRIBDIR}/python ${CONTRIBDIR}/features
.PATH:	${CONTRIBDIR}/tui

VERSION!= cat ${CONTRIBDIR}/version.in

CFLAGS+= -I${.OBJDIR} -I${.CURDIR} -I${BASEDIR}/include -I${BASEDIR}/bfd
CFLAGS+= -I${CONTRIBDIR} -I${CONTRIBDIR}/common
CFLAGS+= -I${BASEDIR}/libdecnumber
CFLAGS+= -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd
CFLAGS+= -I${BASEDIR} 
CFLAGS+= -DMACHINE_ARCH='"${MACHINE_ARCH}"'
CFLAGS+= -DTARGET_ARCH='"${TARGET_ARCH}"'
CFLAGS+= -DVERSION_STRING='"${VERSION}"'
CFLAGS+= -DTUI				# enable "gdb -tui" (TUI MODE)

SRCS+=	observer.h gdb_stdint.h

# The order of these sources is important,
# because init.c below needs to obey a certain ordering
# of function calls.

# COMMON_OBS
SRCS+=	\
	version.c \
	annotate.c \
	addrmap.c \
	auto-load.c auxv.c \
	agent.c \
	bfd-target.c \
	blockframe.c breakpoint.c break-catch-sig.c \
	findvar.c regcache.c cleanups.c \
	charset.c continuations.c corelow.c disasm.c dummy-frame.c dfp.c \
	source.c value.c eval.c valops.c valarith.c valprint.c printcmd.c \
	block.c symtab.c psymtab.c symfile.c symmisc.c linespec.c dictionary.c \
	infcall.c \
	infcmd.c infrun.c \
	expprint.c environ.c stack.c thread.c \
	exceptions.c \
	filesystem.c \
	inf-child.c \
	interps.c \
	minidebug.c \
	main.c \
	macrotab.c macrocmd.c macroexp.c macroscope.c \
	mi-common.c \
	event-loop.c event-top.c inf-loop.c completer.c \
	gdbarch.c arch-utils.c gdbtypes.c gdb_bfd.c gdb_obstack.c \
	osabi.c copying.c \
	memattr.c mem-break.c target.c parse.c language.c buildsym.c \
	findcmd.c \
	std-regs.c \
	signals.c \
	exec.c reverse.c \
	bcache.c objfiles.c observer.c minsyms.c maint.c demangle.c \
	dbxread.c coffread.c coff-pe-read.c \
	dwarf2read.c mipsread.c stabsread.c corefile.c \
	dwarf2expr.c dwarf2loc.c dwarf2-frame.c dwarf2-frame-tailcall.c \
	ada-lang.c c-lang.c d-lang.c f-lang.c objc-lang.c \
	ada-tasks.c ada-varobj.c \
	ui-out.c cli-out.c \
	varobj.c vec.c \
	go-lang.c go-valprint.c go-typeprint.c \
	jv-lang.c jv-valprint.c jv-typeprint.c \
	m2-lang.c opencl-lang.c p-lang.c p-typeprint.c p-valprint.c \
	sentinel-frame.c \
	complaints.c typeprint.c \
	ada-typeprint.c c-typeprint.c f-typeprint.c m2-typeprint.c \
	ada-valprint.c c-valprint.c cp-valprint.c d-valprint.c f-valprint.c \
	m2-valprint.c \
	serial.c mdebugread.c top.c utils.c \
	ui-file.c \
	user-regs.c \
	frame.c frame-unwind.c doublest.c \
	frame-base.c \
	inline-frame.c \
	gnu-v2-abi.c gnu-v3-abi.c cp-abi.c cp-support.c \
	cp-namespace.c \
	reggroups.c regset.c \
	trad-frame.c \
	tramp-frame.c \
	solib.c solib-target.c \
	prologue-value.c memory-map.c memrange.c \
	xml-support.c xml-syscall.c xml-utils.c \
	target-descriptions.c target-memory.c xml-tdesc.c xml-builtin.c \
	inferior.c osdata.c gdb_usleep.c record.c record-full.c gcore.c \
	gdb_vecs.c jit.c progspace.c skip.c probe.c \
	common-utils.c buffer.c ptid.c gdb-dlfcn.c common-agent.c \
	format.c registry.c btrace.c record-btrace.c

# TSOBS
SRCS+=	inflow.c

# SER_HARDWIRE
SRCS+=	ser-base.c ser-unix.c ser-pipe.c ser-tcp.c

# REMOTE_OBS
SRCS+=	remote.c dcache.c tracepoint.c ax-general.c ax-gdb.c \
	remote-fileio.c remote-notif.c

# SUBDIR_CLI_OBS
SRCS+=  cli-dump.c \
	cli-decode.c cli-script.c cli-cmds.c cli-setshow.c \
	cli-logging.c \
	cli-interp.c cli-utils.c

# SUBDIR_MI_OBS
SRCS+=	mi-out.c mi-console.c \
	mi-cmds.c mi-cmd-catch.c mi-cmd-env.c \
	mi-cmd-var.c mi-cmd-break.c mi-cmd-stack.c \
	mi-cmd-file.c mi-cmd-disas.c mi-symbol-cmds.c \
	mi-cmd-target.c mi-cmd-info.c mi-interp.c \
	mi-main.c mi-parse.c mi-getopt.c

# PYTHON OBJS
SRCS+=	python.c \
	py-auto-load.c \
	py-value.c \
	py-prettyprint.c 

# CONFIG_OBS
SRCS+=	elfread.c posix-hdep.c stap-probe.c

# SUBDIR_TUI_OBS
SRCS+=	tui-command.c \
	tui-data.c \
	tui-disasm.c \
	tui-file.c \
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
	tui-winsource.c \
	tui.c

# YYOBJ
SRCS+=	c-exp.y \
	cp-name-parser.y \
	ada-exp.y \
	jv-exp.y \
	f-exp.y \
	go-exp.y \
	m2-exp.y \
	p-exp.y

SRCS+=	init.c

XMLFILES= \
	gdb-target.dtd \
	xinclude.dtd \
	library-list.dtd \
	library-list-svr4.dtd \
	osdata.dtd \
	threads.dtd \
	traceframe-info.dtd \
	btrace.dtd

DPADD=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM}
DPADD+=	../libbfd/libbfd.a ../libopcodes/libopcodes.a ../libiberty/libiberty.a
LDADD=	-ledit -ltermcap -lm
LDADD+=	-L../libbfd -lbfd -L../libopcodes -lopcodes -L../libiberty -liberty

.include "Makefile.${TARGET_ARCH}"

init.c: ${SRCS:Ninit.c} Makefile
	echo '/* Do not modify this file.  */' > ${.TARGET}
	echo '/* It is created automatically by the Makefile.  */' >> ${.TARGET}
	echo '#include "defs.h"      /* For initialize_file_ftype.  */' >> ${.TARGET}
	echo 'extern void initialize_all_files(void);' >> ${.TARGET}
	sed -n -e 's/^_initialize_\([a-z_0-9A-Z]*\).*/extern initialize_file_ftype _initialize_\1;/p' ${.ALLSRC:M*.c} >> ${.TARGET}
	echo 'void' >> ${.TARGET}
	echo 'initialize_all_files (void)' >> ${.TARGET}
	echo '{' >> ${.TARGET}
	sed -n -e 's/^_initialize_\([a-z_0-9A-Z]*\).*/  _initialize_\1 ();/p' ${.ALLSRC:M*.c} >> ${.TARGET}
	echo '}' >> ${.TARGET}

CLEANFILES+=	init.c

xml-builtin.c: feature_to_c.sh ${XMLFILES}
	rm -f ${.TARGET}
	sh ${CONTRIBDIR}/features/feature_to_c.sh ${.TARGET} ${.ALLSRC:M*.dtd}

beforedepend:
	cp ${CONTRIBDIR}/common/agent.c ${.OBJDIR}/common-agent.c
	mkdir -p ${.OBJDIR}/build-gnulib
	cp ${.CURDIR}/../libgnu/config.h ${.OBJDIR}/build-gnulib

CLEANFILES+=	xml-builtin.c common-agent.c
CLEANDIRS+=	build-gnulib

# observer.inc can't be included in SRCS, it would duplicate the entry in OBJS
observer.h: observer.sh observer.inc doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh h ${CONTRIBDIR}/doc/observer.texi ${.TARGET}
observer.inc: observer.sh doc/observer.texi
	sh ${CONTRIBDIR}/observer.sh inc ${CONTRIBDIR}/doc/observer.texi ${.TARGET}

gdb_stdint.h:
	touch ${.TARGET}

CLEANFILES+=	observer.h observer.inc gdb_stdint.h


.include <bsd.lib.mk>
