# $FreeBSD: src/share/mk/bsd.cpu.mk,v 1.2.2.5 2002/07/19 08:09:32 ru Exp $
# $DragonFly: src/share/mk/bsd.cpu.mk,v 1.9 2004/06/21 03:48:07 dillon Exp $

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

CCVER ?= gcc2
_CCVER := ${CCVER}
HOST_CCVER?= ${_CCVER}

.if ${CCVER} == "gcc2"
.  include <bsd.cpu.gcc2.mk>
.elif ${CCVER} == "gcc3"
.  include <bsd.cpu.gcc3.mk>
.elif ${CCVER} == "gcc34"
.  include <bsd.cpu.gcc34.mk>
.elif defined(CCVER_BSD_CPU_MK)
.  if ${CCVER_BSD_CPU_MK} != ""
.    include "${CCVER_BSD_CPU_MK}"
.  endif
.else
.error "Either set CCVER to a known compiler or specify CCVER_BSD_CPU_MK"
.endif

# /usr/bin/cc depend on the CCVER environment variable, make sure CCVER is
# exported for /usr/bin/cc and friends.  Note that CCVER is unsupported when
# cross compiling from 4.x or older versions of DFly and should not be set
# by the user.
#
.if defined(.DIRECTIVE_MAKEENV)
.makeenv CCVER
.makeenv HOST_CCVER
.endif

# We can reassign _CPUCFLAGS and CFLAGS will evaluate properly to the
# new value, we do not have to add the variable to CFLAGS twice.
#
.if !defined(NO_CPU_CFLAGS) && !defined(_CPUCFLAGS_ASSIGNED)
_CPUCFLAGS_ASSIGNED=TRUE
CFLAGS += ${_CPUCFLAGS}
.endif

