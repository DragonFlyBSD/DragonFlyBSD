# $FreeBSD: src/sys/modules/acpi/acpi/Makefile,v 1.3 2004/01/08 16:38:32 njl Exp $
# $DragonFly: src/sys/dev/acpica5/Makefile,v 1.15 2007/01/17 17:31:19 y0netan1 Exp $

CONTRIBDIR=	${SYSDIR}/${ACPICA_DIR}
PATCHES=	hardware,hwsleep.c.patch include,acglobal.h.patch

SYSDIR?= ${.CURDIR}/../..
.if ACPI_DEBUG_LOCKS || ACPI_DEBUG_MEMMAP
CFLAGS+=	-I${.OBJDIR}/include
.if ACPI_DEBUG_LOCKS
CFLAGS+=	-DACPI_DEBUG_LOCKS=1
.endif
.if ACPI_DEBUG_MEMMAP
CFLAGS+=	-DACPI_DEBUG_MEMMAP=1
.endif
PATCHES+=	include,acpiosxf.h.patch
.endif

.include "Makefile.inc"

.PATH:	${.OBJDIR}					\
	${.OBJDIR}/include				\
	${SYSDIR}/${OSACPI_MD_DIR}			\
	${SYSDIR}/machine/${MACHINE}/isa		\
	${SYSDIR}/bus/pci 				\
	${.CURDIR}/Osd					\
	${.CURDIR}					\
	${ACPICA_KERN_PATHS}

KMOD	= acpi

SRCS+=		${PATCHES}
# ACPI CA sources
SRCS+=	dsfield.c dsinit.c dsmethod.c dsmthdat.c
SRCS+=	dsobject.c dsopcode.c dsutils.c dswexec.c dswload.c
SRCS+=	dswscope.c dswstate.c evevent.c evgpe.c evgpeblk.c
SRCS+=	evmisc.c evregion.c evrgnini.c evsci.c evxface.c
SRCS+=	evxfevnt.c evxfregn.c exconfig.c exconvrt.c excreate.c
SRCS+=	exdump.c exfield.c exfldio.c exmisc.c exmutex.c
SRCS+=	exnames.c exoparg1.c exoparg2.c exoparg3.c exoparg6.c
SRCS+=	exprep.c exregion.c exresnte.c exresolv.c exresop.c
SRCS+=	exstore.c exstoren.c exstorob.c exsystem.c exutils.c
SRCS+=	hwacpi.c hwgpe.c hwregs.c hwsleep.c hwtimer.c
SRCS+=	nsaccess.c nsalloc.c nsdump.c nseval.c nsinit.c
SRCS+=	nsload.c nsnames.c nsobject.c nsparse.c nssearch.c
SRCS+=	nsutils.c nswalk.c nsxfeval.c nsxfname.c nsxfobj.c
SRCS+=	psargs.c psloop.c psopcode.c psparse.c psscope.c
SRCS+=	pstree.c psutils.c pswalk.c psxface.c
SRCS+=	rsaddr.c rscalc.c rscreate.c rsdump.c rsinfo.c rsio.c
SRCS+=	rsirq.c rslist.c rsmemory.c rsmisc.c rsutils.c
SRCS+=	rsxface.c tbfadt.c tbfind.c tbinstal.c
SRCS+=	tbutils.c tbxface.c tbxfroot.c utalloc.c
SRCS+=	utclib.c utcopy.c utdebug.c utdelete.c uteval.c
SRCS+=	utglobal.c utinit.c utmath.c utmisc.c utmutex.c utobject.c
SRCS+=	utresrc.c utstate.c utxface.c

# OSD layer
#
SRCS+=  acpi.c acpi_acad.c acpi_battery.c acpi_button.c acpi_cmbat.c acpi_cpu.c
SRCS+=  acpi_ec.c acpi_isab.c acpi_lid.c
SRCS+=  acpi_package.c
# The PCI part of ACPI5 doesn't work with the current infrastructure
# SRCS+=	acpi_pci.c acpi_pcib.c acpi_pcib_acpi.c acpi_pcib_pci.c
# SRCS+=	acpi_pci_link.c
SRCS+=  acpi_powerres.c acpi_resource.c acpi_thermal.c
SRCS+=  acpi_timer.c
SRCS+=  OsdDebug.c
SRCS+=  OsdHardware.c OsdInterface.c OsdInterrupt.c OsdMemory.c OsdSchedule.c
SRCS+=  OsdStream.c OsdSynch.c OsdTable.c OsdEnvironment.c
SRCS+=  opt_acpi.h opt_bus.h opt_ddb.h
SRCS+=  device_if.h bus_if.h pci_if.h pcib_if.h isa_if.h

# Debugging support
DBSRC=	dbcmds.c dbdisply.c dbexec.c dbfileio.c dbhistry.c
DBSRC+=	dbinput.c dbstats.c dbutils.c dbxface.c
DBSRC+=	dmbuffer.c dmnames.c dmopcode.c dmobject.c dmresrc.c dmresrcl.c
DBSRC+=	dmresrcs.c dmutils.c dmwalk.c

.if ACPI_NO_SEMAPHORES
CFLAGS+=-DACPI_NO_SEMAPHORES
.endif
.if ACPI_DEBUG
CFLAGS+=-DACPI_DEBUG
SRCS+=	${DBSRC}
opt_ddb.h: Makefile
	echo "#define DDB 1" > ${.TARGET}
.else
opt_ddb.h: Makefile
	echo -n > ${.TARGET}
.endif
.if ACPI_USE_LOCAL_CACHE
CFLAGS+= -DACPI_USE_LOCAL_CACHE
SRCS+=		utcache.c
.else
SRCS+=		OsdCache.c
.endif

# Machine-specific code such as sleep/wakeup
SRCS+=	acpi_machdep.c acpi_wakecode.h acpi_wakeup.c
.if ${MACHINE_ARCH} == "i386"
# APIC enumerators
#SRCS+=	madt.c
SRCS+=	pmtimer.c
.endif
CLEANFILES+=	acpi_wakecode.h acpi_wakecode.o acpi_wakecode.bin ${DBSRC}
CLEANFILES+=	platform/acenv.h

acpi_wakecode.h: acpi_wakecode.S
	${MAKE} -f ${SYSDIR}/${OSACPI_MD_DIR}/Makefile \
		MAKESRCPATH=${SYSDIR}/${OSACPI_MD_DIR}

SUBDIR=	acpi_toshiba
all: ${PROG} ${SUBDIR}

.include <bsd.kmod.mk>

# *.o files compiled from the patched C sources in the subdirectories of
# ${OBJDIR} are placed right under ${OBJDIR}, not under each subdirectory
OBJS:=		${OBJS:N*/*}
