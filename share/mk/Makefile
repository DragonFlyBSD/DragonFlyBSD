# $FreeBSD: src/share/mk/Makefile,v 1.27.2.8 2002/07/22 14:21:51 ru Exp $
# $DragonFly: src/share/mk/Makefile,v 1.12 2005/06/05 22:43:19 corecode Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/8/93

FILES=	bsd.README
FILES+=	bsd.cpu.mk bsd.cpu.gcc2.mk bsd.cpu.gcc3.mk bsd.cpu.gcc34.mk
FILES+=	bsd.cpu.gcc40.mk
FILES+=	bsd.dep.mk bsd.doc.mk bsd.files.mk
FILES+=	bsd.incs.mk bsd.info.mk bsd.init.mk
FILES+=	bsd.kmod.mk bsd.hostlib.mk
FILES+=	bsd.lib.mk bsd.libnames.mk bsd.links.mk bsd.man.mk bsd.nls.mk
FILES+=	bsd.obj.mk bsd.own.mk
FILES+=	bsd.port.mk bsd.port.post.mk  bsd.port.pre.mk bsd.port.subdir.mk
FILES+= bsd.dfport.mk bsd.dfport.pre.mk bsd.dfport.post.mk
FILES+=	bsd.hostprog.mk bsd.prog.mk bsd.subdir.mk bsd.sys.mk
FILES+=	sys.mk
NOOBJ=	noobj
FILESDIR=	${BINDIR}/mk

beforeinstall:
	date '+%Y%m%d' > ${DESTDIR}/var/db/port.mkversion

.include <bsd.prog.mk>
