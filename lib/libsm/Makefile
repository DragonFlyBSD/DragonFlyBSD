# $FreeBSD: src/lib/libsm/Makefile,v 1.1.2.2 2002/07/19 18:46:26 ru Exp $
# $DragonFly: src/lib/libsm/Makefile,v 1.11 2006/08/18 03:06:59 gshapiro Exp $

SENDMAIL_DIR=${.CURDIR}/../../contrib/sendmail-8.13.8
.PATH:	${SENDMAIL_DIR}/libsm

CFLAGS+= -I${SENDMAIL_DIR}/sendmail -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNETINET6 -DNEWDB -DNIS -DMAP_REGEX -DNOT_SENDMAIL

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

LIB=	sm

SRCS+=	sm_os.h
SRCS+=	assert.c debug.c errstring.c exc.c heap.c match.c rpool.c \
	strdup.c strerror.c strl.c clrerr.c fclose.c feof.c ferror.c \
	fflush.c fget.c fpos.c findfp.c flags.c fopen.c fprintf.c \
	fpurge.c fput.c fread.c fscanf.c fseek.c fvwrite.c fwalk.c \
	fwrite.c get.c makebuf.c put.c refill.c rewind.c setvbuf.c \
	smstdio.c snprintf.c sscanf.c stdio.c strio.c ungetc.c \
	vasprintf.c vfprintf.c vfscanf.c vprintf.c vsnprintf.c \
	wbuf.c wsetup.c string.c stringf.c \
	xtrap.c strto.c test.c path.c strcasecmp.c strrevcmp.c \
	signal.c clock.c config.c shm.c mbdb.c strexit.c cf.c ldap.c \
	niprop.c mpeix.c memstat.c
CLEANFILES+=sm_os.h

INTERNALLIB=		true

sm_os.h:
	${LN} -sf ${SENDMAIL_DIR}/include/sm/os/sm_os_dragonfly.h sm_os.h

.include <bsd.lib.mk>
