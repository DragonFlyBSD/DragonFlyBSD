# $DragonFly: src/nrelease/Makefile,v 1.8 2004/04/23 02:14:07 dillon Exp $
#

ISODIR ?= /usr/release
ISOFILE ?= ${ISODIR}/dfly.iso
ISOROOT = ${ISODIR}/root
OBJSYS= ${.OBJDIR}/../sys
KERNCONF ?= GENERIC

# note: we use the '${NRLOBJDIR}/nrelease' construct, that is we add
# the additional '/nrelease' manually, as a safety measure.
#
NRLOBJDIR?= /usr/obj

WORLD_CCVER ?= ${CCVER}
KERNEL_CCVER ?= ${CCVER}

release:	check clean buildworld1 buildkernel1 buildiso mkiso

quickrel:	check clean buildworld2 buildkernel2 buildiso mkiso

realquickrel:	check clean buildiso mkiso

check:
	if [ ! -f /usr/local/bin/mkisofs ]; then \
		echo "You need to install the mkisofs port for this target"; \
		exit 1; fi
	if [ ! -f /usr/local/bin/cvsup ]; then \
		echo "You need to install the cvsup port for this target"; \
		exit 1; fi

buildworld1:
	( cd ${.CURDIR}/..; make buildworld CCVER=${WORLD_CCVER} )

buildworld2:
	( cd ${.CURDIR}/..; make -DNOTOOLS -DNOCLEAN buildworld CCVER=${WORLD_CCVER} )

buildkernel1:
	( cd ${.CURDIR}/..; make buildkernel KERNCONF=${KERNCONF} CCVER=${KERNEL_CCVER} )

buildkernel2:
	( cd ${.CURDIR}/..; make -DNOCLEAN buildkernel KERNCONF=${KERNCONF} CCVER=${KERNEL_CCVER} )

# note that we do not want to mess with any /usr/obj directories not related
# to buildworld, buildkernel, or nrelease, so we must supply the proper
# MAKEOBJDIRPREFIX for targets that are not run through the buildworld and 
# buildkernel mechanism.
#
buildiso:
	if [ ! -d ${ISOROOT} ]; then mkdir -p ${ISOROOT}; fi
	if [ ! -d ${NRLOBJDIR}/nrelease ]; then mkdir -p ${NRLOBJDIR}/nrelease; fi
	( cd ${.CURDIR}/..; make DESTDIR=${ISOROOT} installworld )
	( cd ${.CURDIR}/../etc; MAKEOBJDIRPREFIX=${NRLOBJDIR}/nrelease make DESTDIR=${ISOROOT} distribution )
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
		-R -J -V DragonFly -o ${ISOFILE} . )

clean:
	if [ -d ${ISOROOT} ]; then chflags -R noschg ${ISOROOT}; fi
	if [ -d ${ISOROOT} ]; then rm -rf ${ISOROOT}; fi
	if [ -d ${NRLOBJDIR}/nrelease ]; then rm -rf ${NRLOBJDIR}/nrelease; fi

realclean:	clean
	rm -rf ${OBJSYS}/${KERNCONF}

.include <bsd.prog.mk>
