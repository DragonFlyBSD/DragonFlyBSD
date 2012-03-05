PROG=	evtranalyze

SRCS= xml.c svg.c evtranalyze.c plotter.c
DPADD=	${LIBEVTR} ${LIBM} ${LIBPROP}
LDADD=  -levtr -lm -lprop
.include <bsd.prog.mk>
