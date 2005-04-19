# $FreeBSD: src/share/mk/bsd.port.mk,v 1.303.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.mk,v 1.31 2005/04/19 17:17:03 joerg Exp $

PORTSDIR?=	/usr/ports
DFPORTSDIR?=	/usr/dfports
PORTPATH!=	/usr/bin/relpath ${PORTSDIR} ${.CURDIR}

.if !defined(DFOSVERSION)
DFOSVERSION!=	/sbin/sysctl -n kern.osreldate
.endif

# Temporary Hack
#
OSVERSION ?= 480102
UNAME_s?= FreeBSD
UNAME_v?=FreeBSD 4.8-CURRENT
UNAME_r?=4.8-CURRENT

# override for bsd.port.mk
PERL_VERSION?=	5.8.6
PERL_VER?=	5.8.6

.makeenv UNAME_s
.makeenv UNAME_v
.makeenv UNAME_r
.makeenv OSVERSION

# handle both native and override ports
.undef USE_GETOPT_LONG

.if defined(USE_RCORDER)
_DF_USE_RCORDER:=	${USE_RCORDER}
.endif
.if defined(USE_RC_SUBR)
_DF_USE_RC_SUBR:=	${USE_RC_SUBR}
.endif

.if !target(install-rc-script)
install-rc-script:
.if defined(USE_RCORDER) || defined(USE_RC_SUBR) && ${USE_RC_SUBR:U} != "YES"
.if defined(USE_RCORDER)
	@${ECHO_CMD} "===> Installing early rcNG startup script(s)"
	@${ECHO_CMD} "@cwd /" >> ${TMPPLIST}
	@for i in ${_DF_USE_RCORDER}; do \
		${INSTALL_SCRIPT} ${WRKDIR}/$${i} /etc/rc.d/$${i%.sh}; \
		${ECHO_CMD} "etc/rc.d/$${i%.sh}" >> ${TMPPLIST}; \
	done
	@${ECHO_CMD} "@cwd ${PREFIX}" >> ${TMPPLIST}
.endif
.if defined(USE_RC_SUBR) && ${USE_RC_SUBR:U} != "YES"
	@${ECHO_CMD} "===> Installing rcNG startup script(s)"
	@${ECHO_CMD} "@cwd ${PREFIX}" >> ${TMPPLIST}
	@for i in ${_DF_USE_RC_SUBR}; do \
		${INSTALL_SCRIPT} ${WRKDIR}/$${i} ${PREFIX}/etc/rc.d/$${i%.sh}.sh; \
		${ECHO_CMD} "etc/rc.d/$${i%.sh}.sh" >> ${TMPPLIST}; \
	done
.endif
.else
	@${DO_NADA}
.endif
.endif

.if defined(USE_RC_SUBR) || defined(USE_RCORDER)
RC_SUBR=	/etc/rc.subr
SUB_LIST+=	RC_SUBR=${RC_SUBR}
.if defined(USE_RC_SUBR) && ${USE_RC_SUBR:U} != "YES"
SUB_FILES+=	${_DF_USE_RC_SUBR}
.endif
.if defined(USE_RCORDER)
SUB_FILES+=	${_DF_USE_RCORDER}
.endif
.endif

.undef USE_RC_SUBR
.undef USE_RCORDER

.if !exists(${DFPORTSDIR}/${PORTPATH}/Makefile)

.if defined(USE_GCC)
.  if ${USE_GCC} == 3.4
.undef USE_GCC
CCVER=	gcc34
.makeenv CCVER
.  endif
.endif

# If the port does not exist in /usr/dfports/<portpath> use the original
# FreeBSD port.  Also process as per normal if BEFOREPORTMK is set so
# any expected variables are set.
#
.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

.else

.if !defined(BEFOREPORTMK)
.undef PORTSDIR
.endif

.undef BEFOREPORTMK
.undef AFTERPORTMK

# Otherwise retarget to the DragonFly override port.
#

TARGETS+=	all
TARGETS+=	build
TARGETS+=	checksum
TARGETS+=	clean
TARGETS+=	clean-for-cdrom
TARGETS+=	clean-for-cdrom-list
TARGETS+=	clean-restricted
TARGETS+=	clean-restricted-list
TARGETS+=	configure
TARGETS+=	deinstall
TARGETS+=	depend
TARGETS+=	depends
TARGETS+=	describe
TARGETS+=	distclean
TARGETS+=	extract
TARGETS+=	fetch
TARGETS+=	fetch-list
TARGETS+=	ignorelist
TARGETS+=	makesum
TARGETS+=	maintainer
TARGETS+=	package
TARGETS+=	realinstall
TARGETS+=	reinstall
TARGETS+=	install
TARGETS+=	tags

# WARNING!  Do not use the -B option.  This appears to propogate to the
# gmake (probably because both use the same environment variable, MAKEFLAGS,
# to pass make options) where as of version 3.80 -B means 'always-make',
# which forces all targets, which blows up gnu builds in the ports system
# because it appears to cause the configure.status target to loop.
#
.if !defined(_DFPORTS_REDIRECT)
_DFPORTS_REDIRECT=
.if !make(package-depends-list) && !make(all-depends-list) && \
    !make(run-depends-list) && !make(build-depends-list) && \
    !make(describe) && !make(package-name)
.BEGIN:
	@echo "WARNING, USING DRAGONFLY OVERRIDE ${DFPORTSDIR}/${PORTPATH}"
	cd ${DFPORTSDIR}/${PORTPATH} && ${MAKE} ${.TARGETS}
.else
.BEGIN:
	@cd ${DFPORTSDIR}/${PORTPATH} && ${MAKE} ${.TARGETS} 
.endif
.endif

.if !empty(.TARGETS)
${.TARGETS}:
.else
all:
.MAIN: all
.endif

# Hack to get Makefiles with conditional statements working
XFREE86_VERSION?=	4
ARCH?=			i386
MACHINE_ARCH?=		i386
HAVE_GNOME?=
FILESDIR?=		${.CURDIR}/files
X_WINDOW_SYSTEM?=	xfree86-4
CAT?=			cat
PREFIX?=		/usr
PERL_LEVEL?=		5
LOCALBASE?=		/usr/local
SED?=			/usr/bin/sed
ECHO_CMD?=		echo
GREP?=			/usr/bin/grep
AWK?=			/usr/bin/awk
UNAME?=			/usr/bin/uname
EXPR?=			/bin/expr
HAVE_SDL?=

PKG_SUFX?=		.tgz
PKGNAME!=		cd ${DFPORTSDIR}/${PORTPATH}; ${MAKE} -V PKGNAME
.for _CATEGORY in ${CATEGORIES}
PKGCATEGORY?=   ${_CATEGORY}
.endfor
_PORTDIRNAME=   ${.CURDIR:T}
PORTDIRNAME?=   ${_PORTDIRNAME}
PKGORIGIN?=             ${PKGCATEGORY}/${PORTDIRNAME}
PLIST_SUB!=		cd ${DFPORTSDIR}/${PORTPATH}; ${MAKE} -V PLIST_SUB

PKGREPOSITORYSUBDIR?=   All
PKGREPOSITORY?=         ${PACKAGES}/${PKGREPOSITORYSUBDIR}
.if exists(${PACKAGES})
PKGFILE?=               ${PKGREPOSITORY}/${PKGNAME}${PKG_SUFX}
.else
PKGFILE?=               ${.CURDIR}/${PKGNAME}${PKG_SUFX}
.endif
 
.endif

