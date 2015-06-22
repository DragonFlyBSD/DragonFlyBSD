# $FreeBSD: src/share/mk/bsd.cpu.mk,v 1.2.2.5 2002/07/19 08:09:32 ru Exp $

# include compiler-specific bsd.cpu.mk.  Note that CCVER may or may not
# be passed as an environment variable.  If not set we make it consistent
# within make but do not otherwise export it.
#
# _CCVER is used to detect changes to CCVER made in Makefile's after the
# fact.
#
# HOST_CCVER is used by the native system compiler and defaults to CCVER.
# It is not subject to local CCVER overrides in Makefiles and it is inherited
# by all sub-makes.
#
# If the host system does not have the desired compiler for HOST_CCVER
# we back off to something it probably does have.

_DEFAULT_CCVER=		gcc50
_DEFAULT_BINUTILSVER=	binutils225

CCVER ?= ${_DEFAULT_CCVER}
_CCVER := ${CCVER}
.if exists(/usr/libexec/${_CCVER}/cc) || exists(/usr/libexec/custom/cc)
HOST_CCVER?= ${_CCVER}
.else
HOST_CCVER?= ${_DEFAULT_CCVER}
.endif

.if exists(/usr/libexec/${_DEFAULT_BINUTILSVER}/elf/as)
HOST_BINUTILSVER?=	${_DEFAULT_BINUTILSVER}
.else
HOST_BINUTILSVER!=	basename `find -s /usr/libexec/binutils* -depth 0 | tail -1`
.endif

.if defined(FORCE_CPUTYPE)
CPUTYPE= ${FORCE_CPUTYPE}
.endif

.if defined(CCVER_BSD_CPU_MK)
.  if ${CCVER_BSD_CPU_MK} != ""
.    include "${CCVER_BSD_CPU_MK}"
.  endif
.elif ${CCVER} == gcc47
.  include <bsd.cpu.gcc47.mk>
.elif ${CCVER} == gcc50
.  include <bsd.cpu.gcc50.mk>
.else
.  include <bsd.cpu.custom.mk>
.endif

# /usr/bin/cc depend on the CCVER environment variable, make sure CCVER is
# exported for /usr/bin/cc and friends.  Note that CCVER is unsupported when
# cross compiling from 4.x or older versions of DFly and should not be set
# by the user.
#
.export CCVER
.export HOST_CCVER

# We can reassign _CPUCFLAGS and CFLAGS will evaluate properly to the
# new value, we do not have to add the variable to CFLAGS twice.
#
.if !defined(NO_CPU_CFLAGS) && !defined(_CPUCFLAGS_ASSIGNED)
_CPUCFLAGS_ASSIGNED=TRUE
CFLAGS += ${_CPUCFLAGS}
.endif

