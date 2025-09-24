#	from: @(#)sys.mk	8.2 (Berkeley) 3/21/94
# $FreeBSD: src/share/mk/sys.mk,v 1.45.2.6 2002/12/23 16:33:37 ru Exp $

unix		?=	We run DragonFly, not UNIX.

# Set any local definitions first.  Place this early, it needs
# MACHINE_CPUARCH to be defined
.if defined(.PARSEDIR)
.sinclude <local.sys.mk>
.endif

# If the special target .POSIX appears (without prerequisites or
# commands) before the first noncomment line in the makefile, make shall
# process the makefile as specified by the Posix 1003.2 specification.
# make(1) sets the special macro %POSIX in this case (to the actual
# value "1003.2", for what it's worth).
#
# The rules below use this macro to distinguish between Posix-compliant
# and default behaviour.

.if defined(%POSIX)
.SUFFIXES:	.o .c .y .l .a .sh .f
.else
.SUFFIXES:	.out .a .ln .o .c .cc .cpp .cxx .C .m .F .f .e .r .y .l .S .s .cl .p .h .sh .no .nx
.endif

.LIBS:		.a

AR		?=	ar
NXAR		?=	${NXENV} ${AR}
.if defined(%POSIX)
ARFLAGS		?=	-rv
.else
ARFLAGS		?=	rc
.endif
RANLIB		?=	ranlib
NXRANLIB	?=	${NXENV} ${RANLIB}

AS		?=	as
AFLAGS		?=

AWK		?=	awk

.if defined(%POSIX)
CC		?=	c89
CFLAGS		?=	-pipe -O${WORLD_CCOPTLEVEL}
.else
CC		?=	cc
CFLAGS		?=	-pipe -O${WORLD_CCOPTLEVEL} ${WORLD_CFLAGS}
.endif
CC_LINK		?=	${CC}

# The system cc frontend is not subject to the path, e.g. when buildworld
# is doing cross compiles it may still need the native compiler for things.
#
NXENV		?=	CCVER=${HOST_CCVER} BINUTILSVER=${HOST_BINUTILSVER} OBJFORMAT_PATH=/ PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/usr/pkg/bin
NXCC		?=	${NXENV} ${CC}
NXCC_LINK	?=	${NXENV} ${CC_LINK}

CXX		?=	c++
CXX_LINK	?=	${CXX}
NXCXX		?=	${NXENV} ${CXX}
NXCXX_LINK	?=	${NXENV} ${CXX_LINK}
CXXFLAGS	?=	${CXXINCLUDES} ${CFLAGS:N-std=*:N-Wnested-externs:N-W*-prototypes:N-Wno-pointer-sign:N-Wold-style-definition}
.if !defined(SYSBUILD) && defined(.MAKE.BUILT.BY) && ${.MAKE.BUILT.BY:Mgcc47}
CXXFLAGS	+=	-D_GLIBCXX_USE_CXX11_ABI=0
.endif

CPP		?=	cpp

.if ${.MAKEFLAGS:M-s} == ""
ECHO		?=	echo
ECHODIR		?=	echo
.else
ECHO		?=	true
.if ${.MAKEFLAGS:M-s} == "-s"
ECHODIR		?=	echo
.else
ECHODIR		?=	true
.endif
.endif

.if defined(%POSIX)
FC		?=	fort77
FFLAGS		?=	-O 1
.else
FC		?=	f77
FFLAGS		?=	-O
.endif
EFLAGS		?=

INSTALL		?=	install
LN		?=	ln

LEX		?=	lex
LFLAGS		?=

LD		?=	ld
NXLD		?=	${NXENV} ${LD}
LDFLAGS		?=
NXCFLAGS	?=	${CROSS_CFLAGS} ${CFLAGS:N-mtune*:N-mcpu*:N-march*:N-flto}
NXCXXFLAGS	?=	${CROSS_CFLAGS} ${CFLAGS:N-mtune*:N-mcpu*:N-march*:N-flto:N-std=*}
NXLDLIBS	?=	${LDLIBS}
NXLDFLAGS	?=	-static ${LDFLAGS}

LINT		?=	lint
LINTFLAGS	?=	-chapbx

MAKE		?=	make

OBJC		?=	cc
OBJCFLAGS	?=	${OBJCINCLUDES} ${CFLAGS} -Wno-import

PC		?=	pc
PFLAGS		?=

RC		?=	f77
RFLAGS		?=

SHELL		?=	sh

YACC		?=	yacc
.if defined(%POSIX)
YFLAGS		?=
.else
YFLAGS		?=	-d
.endif

# The 'make' program is expected to define the following.
#
# MACHINE_PLATFORM	platform architecture (pc64, vkernel64, etc.)
# MACHINE		machine architecture (x86_64, etc.)
# MACHINE_ARCH		cpu architecture (x86_64, etc.)
#
.if !defined(MACHINE)
.error "MACHINE was not defined by make"
.endif
.if !defined(MACHINE_ARCH)
.error "MACHINE_ARCH was not defined by make"
.endif

# Backwards compatibility.  There was a time during 1.7 development
# where we tried to rename MACHINE.  This failed and was reverted,
# and MACHINE_PLATFORM was added to make the distinction.  These shims
# prevent buildworld from breaking.
#
.if !defined(MACHINE_PLATFORM)
MACHINE_PLATFORM!=/sbin/sysctl -n hw.platform
.endif

.if defined(%POSIX)
# Posix 1003.2 mandated rules
#
# Quoted directly from the Posix 1003.2 draft, only the macros
# $@, $< and $* have been replaced by ${.TARGET}, ${.IMPSRC}, and
# ${.PREFIX}, resp.

# SINGLE SUFFIX RULES
.c:
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.IMPSRC}

.f:
	${FC} ${FFLAGS} ${LDFLAGS} -o ${.TARGET} ${.IMPSRC}

.sh:
	cp ${.IMPSRC} ${.TARGET}
	chmod a+x ${.TARGET}

# DOUBLE SUFFIX RULES

.c.o:
	${CC} ${CFLAGS} -c ${.IMPSRC}

.f.o:
	${FC} ${FFLAGS} -c ${.IMPSRC}

.y.o:
	${YACC} ${YFLAGS} ${.IMPSRC}
	${CC} ${CFLAGS} -c y.tab.c
	rm -f y.tab.c
	mv y.tab.o ${.TARGET}

.l.o:
	${LEX} ${LFLAGS} ${.IMPSRC}
	${CC} ${CFLAGS} -c lex.yy.c
	rm -f lex.yy.c
	mv lex.yy.o ${.TARGET}

.y.c:
	${YACC} ${YFLAGS} ${.IMPSRC}
	mv y.tab.c ${.TARGET}

.l.c:
	${LEX} ${LFLAGS} ${.IMPSRC}
	mv lex.yy.c ${.TARGET}

.c.a:
	${CC} ${CFLAGS} -c ${.IMPSRC}
	${AR} ${ARFLAGS} ${.TARGET} ${.PREFIX}.o
	rm -f ${.PREFIX}.o

.f.a:
	${FC} ${FFLAGS} -c ${.IMPSRC}
	${AR} ${ARFLAGS} ${.TARGET} ${.PREFIX}.o
	rm -f ${.PREFIX}.o

.else

# non-Posix rule set

.sh:
	cp -p ${.IMPSRC} ${.TARGET}
	chmod a+x ${.TARGET}

.c:
	${CC} ${_${.IMPSRC:T}_FLAGS} ${CFLAGS} ${LDFLAGS} ${.IMPSRC} ${LDLIBS} -o ${.TARGET}

.c.o:
	${CC} ${_${.IMPSRC:T}_FLAGS} ${CFLAGS} -c ${.IMPSRC}

.cc .cpp .cxx .C:
	${CXX} ${_${.IMPSRC:T}_FLAGS} ${CXXFLAGS} ${LDFLAGS} ${.IMPSRC} ${LDLIBS} -o ${.TARGET}

.cc.o .cpp.o .cxx.o .C.o:
	${CXX} ${_${.IMPSRC:T}_FLAGS} ${CXXFLAGS} -c ${.IMPSRC}

.m.o:
	${OBJC} ${_${.IMPSRC:T}_FLAGS} ${OBJCFLAGS} -c ${.IMPSRC}

.p.o:
	${PC} ${_${.IMPSRC:T}_FLAGS} ${PFLAGS} -c ${.IMPSRC}

.e .r .F .f:
	${FC} ${_${.IMPSRC:T}_FLAGS} ${RFLAGS} ${EFLAGS} ${FFLAGS} ${LDFLAGS} \
	    ${.IMPSRC} ${LDLIBS} -o ${.TARGET}

.e.o .r.o .F.o .f.o:
	${FC} ${_${.IMPSRC:T}_FLAGS} ${RFLAGS} ${EFLAGS} ${FFLAGS} -c ${.IMPSRC}

.S.o:
	${CC} ${_${.IMPSRC:T}_FLAGS} ${CFLAGS} -c ${.IMPSRC}

.s.o:
	${AS} ${_${.IMPSRC:T}_FLAGS} ${AFLAGS} -o ${.TARGET} ${.IMPSRC}

# XXX not -j safe
.y.o:
	${YACC} ${YFLAGS} ${.IMPSRC}
	${CC} ${CFLAGS} -c y.tab.c -o ${.TARGET}
	rm -f y.tab.c

.l.o:
	${LEX} -t ${LFLAGS} ${.IMPSRC} > ${.PREFIX}.tmp.c
	${CC} ${CFLAGS} -c ${.PREFIX}.tmp.c -o ${.TARGET}
	rm -f ${.PREFIX}.tmp.c

# .no == native object file, for helper code when cross building.
#
.c.no:
	${NXCC} ${_${.IMPSRC:T}_FLAGS} ${NXCFLAGS:N-flto} -c ${.IMPSRC} -o ${.TARGET}

.cc.no .C.no .cpp.no .cxx.no:
	${NXCXX} ${_${.IMPSRC:T}_FLAGS} ${NXCXXFLAGS:N-flto} -c ${.IMPSRC} -o ${.TARGET}

.y.no:
	${YACC} ${YFLAGS} ${.IMPSRC}
	${NXCC} ${NXCFLAGS} -c y.tab.c -o ${.TARGET}
	rm -f y.tab.c

.l.no:
	${LEX} ${LFLAGS} -o${.TARGET}.c ${.IMPSRC}
	${NXCC} ${NXCFLAGS} -c ${.TARGET}.c -o ${.TARGET}
	rm -f ${.TARGET}.c

.no.nx .c.nx:
	${NXCC} ${_${.IMPSRC:T}_FLAGS} ${NXCFLAGS} ${NXLDFLAGS} ${.IMPSRC} \
	    ${NXLDLIBS} -o ${.TARGET}

# XXX not -j safe
.y.c:
	${YACC} ${YFLAGS} ${.IMPSRC}
	mv y.tab.c ${.TARGET}

.l.c:
	${LEX} -t ${LFLAGS} ${.IMPSRC} > ${.TARGET}

.s.out .c.out .o.out:
	${CC} ${_${.IMPSRC:T}_FLAGS} ${CFLAGS} ${LDFLAGS} ${.IMPSRC} ${LDLIBS} -o ${.TARGET}

.f.out .F.out .r.out .e.out:
	${FC} ${_${.IMPSRC:T}_FLAGS} ${EFLAGS} ${RFLAGS} ${FFLAGS} ${LDFLAGS} \
	    ${.IMPSRC} ${LDLIBS} -o ${.TARGET}
	rm -f ${.PREFIX}.o

# XXX not -j safe
.y.out:
	${YACC} ${YFLAGS} ${.IMPSRC}
	${CC} ${CFLAGS} ${LDFLAGS} y.tab.c ${LDLIBS} -ly -o ${.TARGET}
	rm -f y.tab.c

.l.out:
	${LEX} -t ${LFLAGS} ${.IMPSRC} > ${.PREFIX}.tmp.c
	${CC} ${CFLAGS} ${LDFLAGS} ${.PREFIX}.tmp.c ${LDLIBS} -ll -o ${.TARGET}
	rm -f ${.PREFIX}.tmp.c

.endif

# Include base system defaults.
.if exists(/etc/defaults/make.conf)
.include </etc/defaults/make.conf>
.endif

# XXX we should some how include src tree etc/defaults/make.conf too. Changes
# to default/make.conf only applies after installworld so might produce world
# that no longer can bootstrap itself.

# Private helper for handling alternative compilers and Makefile.inc1 tester.
# This variable is used to choose the one or more alternative compilers to
# build for world.  If set to "all", then all available alternative compilers
# are built.
WORLD_ALTCOMPILER?= gcc47

# Include global user settings.
__MAKE_CONF?=/etc/make.conf
.if exists(${__MAKE_CONF})
.include "${__MAKE_CONF}"
.endif

# Helper for bootstrapping in makefiles.
.if !defined(WORLD_VERSION)
.if defined(.MAKE.DF.VERSION)
WORLD_VERSION=	${.MAKE.DF.VERSION}
.else
.if exists(/usr/include/sys/param.h)
WORLD_VERSION!=	${AWK} '/^\#define[[:blank:]]__DragonFly_version/ {print $$3}' < /usr/include/sys/param.h
.endif
.endif
# Export it to ensure it will stay constant from initial make invoke.
.MAKEFLAGS: WORLD_VERSION=${WORLD_VERSION}
.endif

.include <bsd.cpu.mk>

.if exists(/etc/make.conf.local)
.error Error, original /etc/make.conf should be moved to the /etc/defaults/ directory and /etc/make.conf.local should be renamed to /etc/make.conf.
.include </etc/make.conf.local>
.endif

# Default executable format
# XXX hint for bsd.port.mk
OBJFORMAT?=	elf

# Tell bmake to expand -V VAR by default
.MAKE.EXPAND_VARIABLES= yes

.if !defined(.PARSEDIR)
# Not using bmake, which is aggressive about search .PATH
# It is sometimes necessary to curb its enthusiam with .NOPATH
# The following allows us to quietly ignore .NOPATH when no using bmake.
.NOTMAIN: .NOPATH
.NOPATH:

.endif
