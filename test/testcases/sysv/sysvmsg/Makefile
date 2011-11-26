# $DragonFly: src/tools/regression/sysvmsg/Makefile,v 1.1 2003/10/18 12:13:01 hmp Exp $
# SysV Message Queue Regression Utility
# Obtained from: $NetBSD: Makefile,v 1.3 2002/03/05 21:56:59 thorpej Exp $

PROG=	msgtest
NOMAN=	yes

regress:
	@if ./msgtest ./msgtest; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
	fi

.include <bsd.prog.mk>
