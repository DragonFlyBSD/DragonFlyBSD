.include "../Makefile.inc0"
.PATH: ${SRCDIR}/util ${SRCDIR}/doc

PROG=		texindex
MAN=		texindex.1 texi2dvi.1 texi2pdf.1 pdftexi2dvi.1
SCRIPTS=	texi2dvi texi2pdf pdftexi2dvi

CFLAGS+=	-I${SRCDIR} -I${SRCDIR}/gnulib/lib
CFLAGS+=	-I${.CURDIR}/../libgnu

.include <bsd.prog.mk>

