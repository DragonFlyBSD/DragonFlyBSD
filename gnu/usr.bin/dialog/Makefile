.PATH:	${.CURDIR}/../../../contrib/dialog

PROG=	dialog

DPADD=	$(LIBDIALOG) ${LIBM} $(LIBNCURSESW)
LDADD=	-ldialog -lm -lprivate_ncursesw
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
