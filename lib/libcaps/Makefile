# $DragonFly: src/lib/libcaps/Makefile,v 1.2 2003/12/04 22:06:19 dillon Exp $
#
LIB=caps
SHLIB_MAJOR= 1
SHLIB_MINOR= 0
AINC= -I${.CURDIR}/${MACHINE_ARCH} -I${.OBJDIR}
CFLAGS+= -I${.CURDIR}/.. -I${.CURDIR}/${MACHINE_ARCH}
CFLAGS+= -DSMP -D_UTHREAD
CFLAGS+= -Wall -g -DDEBUG
INSTALL_PIC_ARCHIVE=	yes
.PATH:	${.CURDIR}/${MACHINE_ARCH}  ${.CURDIR}/../../sys/kern

MISRCS=	lwkt_msgport.c lwkt_thread.c \
	uthread.c init.c sysport.c slaballoc.c \
	globaldata.c caps_client.c caps_misc.c caps_service.c

SRCS+= ${.OBJDIR}/asdefs.h
CLEANFILES+=asdefs asdefs.o ${.CURDIR}/asdefs.h

${.OBJDIR}/asdefs.h: asdefs
	${.OBJDIR}/${.ALLSRC} > ${.TARGET}

.include "${MACHINE_ARCH}/Makefile.inc"

##################################################################
# For now this is a clone of what src/lib/libcr/Makefile.inc does.
# When this is liblwkt becomes part of libcr the rest of this file
# can be removed.
##################################################################

# If there are no machine dependent sources, append all the
# machine-independent sources:
.if empty{MDSRCS}
SRCS+=          ${MISRCS}
.else
# Append machine-dependent sources, then append machine-independent sources
# for which there is no machine-dependent variant.
SRCS+=          ${MDSRCS}
.for _src in ${MISRCS}
.if ${MDSRCS:R:M${_src:R}} == ""
SRCS+=          ${_src}
.endif
.endfor
.endif

.include <bsd.lib.mk>
