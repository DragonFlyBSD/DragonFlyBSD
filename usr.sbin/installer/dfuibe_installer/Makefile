#
# $DragonFly: src/usr.sbin/installer/dfuibe_installer/Makefile,v 1.5 2008/08/29 22:51:56 swildner Exp $

PROG=	dfuibe_installer
NOMAN=
SRCS=	flow.c fn_diagnostic.c fn_install.c fn_zonetab.c \
	fn_configure.c fn_disk.c fn_subpart_hammer.c fn_subpart_ufs.c main.c

CFLAGS+=-I${.CURDIR}/..

.if exists(${.OBJDIR}/../libaura)
LIBAURA=${.OBJDIR}/../libaura/libaura.a
.else
LIBAURA=${.CURDIR}/../libaura/libaura.a
.endif

.if exists(${.OBJDIR}/../libdfui)
LIBDFUI=${.OBJDIR}/../libdfui/libdfui.a
.else
LIBDFUI=${.CURDIR}/../libdfui/libdfui.a
.endif

.if exists(${.OBJDIR}/../libinstaller)
LIBINSTALLER=${.OBJDIR}/../libinstaller/libinstaller.a
.else
LIBINSTALLER=${.CURDIR}/../libinstaller/libinstaller.a
.endif

DPADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} ${LIBUTIL}
LDADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} -lutil

.include <bsd.prog.mk>
