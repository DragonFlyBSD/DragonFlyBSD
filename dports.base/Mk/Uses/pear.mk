# Use the PHP Extension and Application Repository
#
# Feature:	pear
# Usage:	USES=pear
# Valid ARGS:	env
#
#	- env : Only provide the environment variables, no fetch/build/install
#		targets.
#
# MAINTAINER=	ports@FreeBSD.org

.if !defined(_INCLUDE_USES_PEAR_MK)
_INCLUDE_USES_PEAR_MK=	yes
_USES_POST+=	pear

_valid_pear_ARGS=		env

# Sanity check
.  for arg in ${pear_ARGS}
.    if empty(_valid_pear_ARGS:M${arg})
IGNORE=	Incorrect 'USES+= pear:${pear_ARGS}' usage: argument [${arg}] is not recognized
.    endif
.  endfor

_pear_IGNORE_WITH_PHP=
IGNORE_WITH_PHP?=	${_pear_IGNORE_WITH_PHP}
php_ARGS+=	flavors
.include "${USESDIR}/php.mk"

# Mark the port ignored if it wants pear for an unsupported flavor
.  if ${_pear_IGNORE_WITH_PHP:tw:S/^/php/:M${PHP_FLAVOR}}
IGNORE=		devel/pear does not support flavor ${PHP_FLAVOR}
_pear_INVALID=	yes
.  endif

.  if !defined(_pear_INVALID)
.    if empty(pear_ARGS:Menv)
MASTER_SITES?=	http://pear.php.net/get/

EXTRACT_SUFX?=	.tgz
DIST_SUBDIR?=	PEAR

WWW?=		https://pear.php.net/package/${PORTNAME}/

.      if empty(php_ARGS:Mphpize)
NO_BUILD=	yes
.      endif
.    endif

BUILD_DEPENDS+=	pear:devel/pear@${PHP_FLAVOR}
RUN_DEPENDS+=	pear:devel/pear@${PHP_FLAVOR}

PEAR_PKGNAMEPREFIX=	php${PHP_VER}-pear-

.    if defined(PEAR_CHANNEL) && ${PEAR_CHANNEL} != ""
PEAR_${PEAR_CHANNEL:tu}_PKGNAMEPREFIX=	php${PHP_VER}-pear-${PEAR_CHANNEL}-
PKGNAMEPREFIX?=	${PEAR_${PEAR_CHANNEL:tu}_PKGNAMEPREFIX}
PEARPKGREF=	${PEAR_CHANNEL}/${PORTNAME}
PEAR_CHANNEL_VER?=	>=0
BUILD_DEPENDS+=	${PEAR_PKGNAMEPREFIX}channel-${PEAR_CHANNEL}${PEAR_CHANNEL_VER}:devel/pear-channel-${PEAR_CHANNEL}@${PHP_FLAVOR}
RUN_DEPENDS+=	${PEAR_PKGNAMEPREFIX}channel-${PEAR_CHANNEL}${PEAR_CHANNEL_VER}:devel/pear-channel-${PEAR_CHANNEL}@${PHP_FLAVOR}
.    else
PKGNAMEPREFIX?=	${PEAR_PKGNAMEPREFIX}
PEARPKGREF=	${PORTNAME}
.    endif

.    if exists(${LOCALBASE}/bin/php-config)
PHP_BASE!=	${LOCALBASE}/bin/php-config --prefix
.    else
PHP_BASE=	${LOCALBASE}
.    endif
PEAR=		${LOCALBASE}/bin/pear
LPEARDIR=	share/pear
LPKGREGDIR=	${LPEARDIR}/packages/${PKGNAME}
LDATADIR=	${LPEARDIR}/data/${PORTNAME}
LDOCSDIR=	share/doc/pear/${PORTNAME}
LEXAMPLESDIR=	share/examples/pear/${PORTNAME}
LSQLSDIR=	${LPEARDIR}/sql/${PORTNAME}
LSCRIPTSDIR=	bin
LTESTSDIR=	${LPEARDIR}/tests/${PORTNAME}
PEARDIR=	${PHP_BASE}/${LPEARDIR}
PKGREGDIR=	${PHP_BASE}/${LPKGREGDIR}
DATADIR=	${PHP_BASE}/${LDATADIR}
DOCSDIR=	${PHP_BASE}/${LDOCSDIR}
EXAMPLESDIR=	${PHP_BASE}/${LEXAMPLESDIR}
SQLSDIR=	${PHP_BASE}/${LSQLSDIR}
SCRIPTFILESDIR=	${LOCALBASE}/bin
TESTSDIR=	${PHP_BASE}/${LTESTSDIR}
.    if defined(CATEGORY) && !empty(CATEGORY)
LINSTDIR=	${LPEARDIR}/${CATEGORY}
.    else
LINSTDIR=	${LPEARDIR}
.    endif
INSTDIR=	${PHP_BASE}/${LINSTDIR}

SUB_LIST+=	PKG_NAME=${PEARPKGREF}

.    if empty(pear_ARGS:Menv)
.      if empty(php_ARGS:Mphpize) && !exists(${.CURDIR}/pkg-plist)
PLIST=		${WRKDIR}/PLIST
.      endif
PKGINSTALL?=	${PORTSDIR}/devel/pear/pear-install
PKGDEINSTALL?=	${WRKDIR}/pear-deinstall
.    endif

PLIST_SUB+=	PEARDIR=${LPEARDIR} PKGREGDIR=${LPKGREGDIR} \
		TESTSDIR=${LTESTSDIR} INSTDIR=${LINSTDIR} SQLSDIR=${LSQLSDIR} \
		SCRIPTFILESDIR=${LCRIPTSDIR}

.  endif # !defined(_pear_INVALID)
.endif

.if defined(_POSTMKINCLUDED) && !defined(_INCLUDE_USES_PEAR_POST_MK) && !defined(_pear_INVALID)
_INCLUDE_USES_PEAR_POST_MK=	yes

.  if empty(pear_ARGS:Menv)

_USES_install+=	250:pear-pre-install
pear-pre-install:
.    if exists(${LOCALBASE}/lib/php.DIST_PHP)	\
	|| exists(${PHP_BASE}/lib/php.DIST_PHP)	\
	|| exists(${LOCALBASE}/.PEAR.pkg)	\
	|| exists(${PHP_BASE}/.PEAR.pkg)
	@${ECHO_MSG} ""
	@${ECHO_MSG} "	Sorry, the PEAR structure has been modified;"
	@${ECHO_MSG} "	Please deinstall your installed pear- ports."
	@${ECHO_MSG} ""
	@${FALSE}
.    endif
	(if [ -f ${WRKSRC}/package.xml ]	\
	&& [ ! -f ${WRKDIR}/package.xml ] ; then	\
		${CP} -p ${WRKSRC}/package.xml ${WRKDIR} ;	\
	fi)

DIRFILTER=	${SED} -En '\:^.*/[^/]*$$:s:^(.+)/[^/]*$$:\1:p' \
		    | ( while read r; do \
			C=1; \
			while [ $$C = 1 ]; do \
			    echo $$r; \
			    if echo $$r | ${GREP} '/' > /dev/null; then \
	                        r=`${DIRNAME} $$r`; \
			    else  \
	                        C=0; \
	                    fi; \
	                done; \
	            done \
	      ) | ${SORT} -ur

.    if empty(php_ARGS:Mphpize)
_USES_install+=	260:do-autogenerate-plist
do-autogenerate-plist:
	@${ECHO_MSG} "===>   Generating packing list with pear"
	@${LN} -sf ${WRKDIR}/package.xml ${WRKSRC}/package.xml
	@cd ${WRKSRC} && ${PEAR} install -n -f -P ${WRKDIR}/inst package.xml > /dev/null 2> /dev/null
.      for R in .channels .depdb .depdblock .filemap .lock .registry
	@${RM} -r ${WRKDIR}/inst/${PREFIX}/${LPEARDIR}/${R}
	@${RM} -r ${WRKDIR}/inst/${R}
.      endfor
	@FILES=`cd ${WRKDIR}/inst && ${FIND} . -type f | ${CUT} -c 2- | \
	${GREP} -v -E "^${PREFIX}/"` || exit 0; \
	${ECHO_CMD} $${FILES}; if ${TEST} -n "$${FILES}"; then \
	${ECHO_CMD} "Cannot generate packing list: package files outside PREFIX"; \
	exit 1; fi;
	@${ECHO_CMD} "${LPKGREGDIR}/package.xml" > ${PLIST}
# pkg_install needs to escape $ in directory name while pkg does not
	@cd ${WRKDIR}/inst/${PREFIX} && ${FIND} . -type f | ${SORT} \
	| ${CUT} -c 3- >> ${PLIST}

do-install:
	@cd ${WRKSRC} && ${PEAR} install -n -f -P ${STAGEDIR} package.xml
# Clean up orphans re-generated by pear-install
.      for R in .channels .depdb .depdblock .filemap .lock .registry
	@${RM} -r ${STAGEDIR}${PREFIX}/${LPEARDIR}/${R}
	@${RM} -r ${STAGEDIR}/${R}
.      endfor
.    endif

_USES_install+=	270:do-generate-deinstall-script
do-generate-deinstall-script:
	@${SED} ${_SUB_LIST_TEMP} -e '/^@comment /d' ${PORTSDIR}/devel/pear/pear-deinstall.in > ${WRKDIR}/pear-deinstall

_USES_install+=	550:pear-post-install
pear-post-install:
	@${MKDIR} ${STAGEDIR}${PKGREGDIR}
	@${INSTALL_DATA} ${WRKDIR}/package.xml ${STAGEDIR}${PKGREGDIR}

show-depends: patch
	@${PEAR} package-dependencies ${WRKDIR}/package.xml

.  endif

.endif
