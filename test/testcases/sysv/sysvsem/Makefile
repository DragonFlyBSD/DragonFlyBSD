# $DragonFly: src/tools/regression/sysvsem/Makefile,v 1.1 2003/10/18 12:13:01 hmp Exp $
# SysV Semaphores Regression Utility 
# Obtained from: $NetBSD: Makefile,v 1.3 2002/03/05 21:57:00 thorpej Exp $

PROG=	semtest
NOMAN=	yes

regress:
	@if ./semtest ./semtest; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
	fi

.include <bsd.prog.mk>
