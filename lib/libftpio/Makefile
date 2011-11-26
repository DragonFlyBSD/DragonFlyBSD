# $FreeBSD: src/lib/libftpio/Makefile,v 1.9.2.2 2001/04/25 10:04:30 ru Exp $

LIB=		ftpio
SHLIB_MAJOR=	7

SRCS=		ftpio.c ftperr.c
INCS=		ftpio.h
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-DINET6
MAN=		ftpio.3
MLINKS+=	ftpio.3 ftpAscii.3
MLINKS+=	ftpio.3 ftpBinary.3
MLINKS+=	ftpio.3 ftpChdir.3
MLINKS+=	ftpio.3 ftpErrno.3
MLINKS+=	ftpio.3 ftpErrString.3
MLINKS+=	ftpio.3 ftpGet.3
MLINKS+=	ftpio.3 ftpGetModtime.3
MLINKS+=	ftpio.3 ftpGetSize.3
MLINKS+=	ftpio.3 ftpGetURL.3
MLINKS+=	ftpio.3 ftpGetURLAf.3
MLINKS+=	ftpio.3 ftpLogin.3
MLINKS+=	ftpio.3 ftpLoginAf.3
MLINKS+=	ftpio.3 ftpPassive.3
MLINKS+=	ftpio.3 ftpPut.3
MLINKS+=	ftpio.3 ftpPutURL.3
MLINKS+=	ftpio.3 ftpPutURLAf.3
MLINKS+=	ftpio.3 ftpVerbose.3
CLEANFILES=	ftperr.c

ftperr.c:	ftp.errors
	@echo '#include <stdio.h>' > ${.TARGET}
	@echo '#include "ftpio.h"' >> ${.TARGET}
	@echo "struct ftperr ftpErrList[] = {" \ >>  ${.TARGET}
	@cat ${.ALLSRC} \
	  | grep -v ^# \
	  | sort \
	  | while read NUM STRING; do \
	    echo "  { $${NUM}, \"$${STRING}\" },"; \
	  done >> ${.TARGET}
	@echo "};" >> ${.TARGET}
	@echo -n "int const ftpErrListLength = " >> ${.TARGET}
	@echo "sizeof(ftpErrList) / sizeof(*ftpErrList);" >> ${.TARGET}

.include <bsd.lib.mk>
