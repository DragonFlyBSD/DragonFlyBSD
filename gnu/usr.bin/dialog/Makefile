.PATH:	${.CURDIR}/../../../contrib/dialog

PROG=	dialog

DPADD=	$(LIBDIALOG) ${LIBM} $(LIBNCURSESW)
LDADD=	-ldialog -lm -lprivate_ncursesw

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
