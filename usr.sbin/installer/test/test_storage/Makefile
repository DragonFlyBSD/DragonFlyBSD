PROG=	test_storage
NOMAN=

.PATH: ${.CURDIR}/../../libdfui

CFLAGS+=-I${.CURDIR}/../..
SRCS+= dump.c test_storage.c
#CFLAGS+=-DDEBUG

.if exists(${.OBJDIR}/../../libaura)
LIBAURA=${.OBJDIR}/../../libaura/libaura.a
.else
LIBAURA=${.CURDIR}/../../libaura/libaura.a
.endif

.if exists(${.OBJDIR}/../../libdfui)
LIBDFUI=${.OBJDIR}/../../libdfui/libdfui.a
.else
LIBDFUI=${.CURDIR}/../../libdfui/libdfui.a
.endif

.if exists(${.OBJDIR}/../../libinstaller)
LIBINSTALLER=${.OBJDIR}/../../libinstaller/libinstaller.a
.else
LIBINSTALLER=${.CURDIR}/../../libinstaller/libinstaller.a
.endif
DPADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} ${LIBUTIL}
LDADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} -lutil

.include <bsd.prog.mk>
