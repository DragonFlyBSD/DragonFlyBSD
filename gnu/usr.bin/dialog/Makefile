.PATH:	${.CURDIR}/../../../contrib/dialog

PROG=	dialog

DPADD=	$(LIBDIALOG) ${LIBM} $(LIBNCURSESW)
LDADD=	-ldialog -lm -lprivate_ncursesw
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
