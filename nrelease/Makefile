# $DragonFly: src/nrelease/Makefile,v 1.3 2003/12/01 19:12:25 dillon Exp $
#
ISODIR ?= /usr/release
ISOFILE ?= ${ISODIR}/dfly.iso
ISOROOT = ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC

#.if !exists(/usr/local/bin/mkisofs)
#.error "You need to install the mkisofs port so I can build the ISO"
#.endif
#.if !exists(/usr/local/bin/cvsup)
#.error "You need to install the cvsup port so I can put cvsup in the ISO"
#.endif

release:	clean buildworld1 buildkernel1 buildiso mkiso

quickrel:	clean buildworld2 buildkernel2 buildiso mkiso

realquickrel:	clean buildiso mkiso

buildworld1:
	( cd ${.CURDIR}/..; make buildworld )

buildworld2:
	( cd ${.CURDIR}/..; make -DNOTOOLS -DNOCLEAN buildworld )

buildkernel1:
	( cd ${.CURDIR}/..; make buildkernel KERNCONF=${KERNCONF} )

buildkernel2:
	( cd ${.CURDIR}/..; make -DNOCLEAN buildkernel KERNCONF=${KERNCONF} )

buildiso:
	if [ ! -d ${ISOROOT} ]; then mkdir -p ${ISOROOT}; fi
	( cd ${.CURDIR}/..; make DESTDIR=${ISOROOT} installworld )
	( cd ${.CURDIR}/../etc; make DESTDIR=${ISOROOT} distribution )
	cpdup -X cpignore -o ${.CURDIR}/root ${ISOROOT} -vv
	( cd ${.CURDIR}/..; make DESTDIR=${ISOROOT} \
		installkernel KERNCONF=${KERNCONF} )
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.local.dist -p ${ISOROOT}/usr/local/
	mtree -deU -f ${.CURDIR}/../etc/mtree/BSD.var.dist -p ${ISOROOT}/var
	dev_mkdb -f ${ISOROOT}/var/run/dev.db ${ISOROOT}/dev
	cp /usr/local/bin/mkisofs ${ISOROOT}/usr/local/bin
	cp /usr/local/man/man8/mkisofs.8.gz ${ISOROOT}/usr/local/man/man8
	cp /usr/local/bin/cvsup ${ISOROOT}/usr/local/bin
	cp /usr/local/man/man1/cvsup.1.gz ${ISOROOT}/usr/local/man/man1

mkiso:
	( cd ${ISOROOT}; mkisofs -b boot/cdboot -no-emul-boot \
		-r -J -V DragonFly -o ${ISOFILE} . )

clean:
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	if [ -d ${ISOROOT} ]; then rm -rf ${ISOROOT}; fi

realclean:	clean
	rm -rf ${OBJSYS}/GENERIC

.include <bsd.prog.mk>
