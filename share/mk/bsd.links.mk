# $FreeBSD: src/share/mk/bsd.links.mk,v 1.2.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/bsd.links.mk,v 1.3 2005/07/07 11:49:56 corecode Exp $

.if !target(__<bsd.init.mk>__)
.error bsd.links.mk cannot be included directly.
.endif

afterinstall: _installlinks
.ORDER: realinstall _installlinks
_installlinks:
.if defined(LINKS) && !empty(LINKS)
	@set ${LINKS}; \
	while test $$# -ge 2; do \
		l=${DESTDIR}$$1; \
		shift; \
		t=${DESTDIR}$$1; \
		shift; \
		${ECHO} $$t -\> $$l; \
		${LN} -f $$l $$t; \
	done; true
.endif
.if defined(SYMLINKS) && !empty(SYMLINKS)
	@set ${SYMLINKS}; \
	while test $$# -ge 2; do \
		l=$$1; \
		shift; \
		t=${DESTDIR}$$1; \
		shift; \
		${ECHO} $$t -\> $$l; \
		${LN} -fhs $$l $$t; \
	done; true
.endif
