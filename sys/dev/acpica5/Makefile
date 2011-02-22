# $FreeBSD: src/sys/modules/acpi/acpi/Makefile,v 1.3 2004/01/08 16:38:32 njl Exp $
# $DragonFly: src/sys/dev/acpica5/Makefile,v 1.23 2008/08/28 09:25:42 hasso Exp $

CONTRIBDIR=	${SYSDIR}/${ACPICA_DIR}

SYSDIR?= ${.CURDIR}/../..
.if ACPI_DEBUG_LOCKS || ACPI_DEBUG_MEMMAP || ACPI_DEBUG_CACHE
CFLAGS+=	-I${.OBJDIR}/include
.if ACPI_DEBUG_LOCKS
CFLAGS+=	-DACPI_DEBUG_LOCKS=1
.endif
.if ACPI_DEBUG_MEMMAP
CFLAGS+=	-DACPI_DEBUG_MEMMAP=1
.endif
.if ACPI_DEBUG_CACHE
CFLAGS+=	-DACPI_DEBUG_CACHE=1
.endif
.endif

.include "Makefile.inc"

.PATH:	${.OBJDIR}					\
	${.OBJDIR}/include				\
	${SYSDIR}/${OSACPI_MD_DIR}			\
	${SYSDIR}/platform/${MACHINE_PLATFORM}/isa	\
	${SYSDIR}/bus/pci 				\
	${.CURDIR}/Osd					\
	${.CURDIR}					\
	${ACPICA_KERN_PATHS}

KMOD	= acpi

# ACPI CA sources
SRCS+=	dbcmds.c dbdisply.c dbexec.c dbfileio.c dbhistry.c
SRCS+=	dbinput.c dbstats.c dbutils.c dbxface.c
SRCS+=	dbmethod.c dbnames.c
SRCS+=	dmbuffer.c dmnames.c dmobject.c dmopcode.c dmresrc.c dmresrcl.c
SRCS+=	dmresrcs.c dmutils.c dmwalk.c
SRCS+=	dsfield.c dsinit.c dsmethod.c dsmthdat.c
SRCS+=	dsobject.c dsopcode.c dsutils.c dswexec.c dswload.c
SRCS+=	dswscope.c dswstate.c dsargs.c dscontrol.c dswload2.c
SRCS+=  evevent.c evgpe.c evgpeblk.c evgpeinit.c evgpeutil.c evxfgpe.c
SRCS+=	evmisc.c evregion.c evrgnini.c evsci.c evxface.c
SRCS+=	evxfevnt.c evxfregn.c exconfig.c exconvrt.c excreate.c
SRCS+=	exdump.c exfield.c exfldio.c exmisc.c exmutex.c
SRCS+=	exnames.c exoparg1.c exoparg2.c exoparg3.c exoparg6.c
SRCS+=	exprep.c exregion.c exresnte.c exresolv.c exresop.c
SRCS+=	exstore.c exstoren.c exstorob.c exsystem.c exutils.c exdebug.c
SRCS+=	hwacpi.c hwgpe.c hwregs.c hwsleep.c hwtimer.c hwvalid.c hwxface.c hwpci.c
SRCS+=	nsaccess.c nsalloc.c nsdump.c nseval.c nsinit.c nsrepair.c nsrepair2.c
SRCS+=	nsload.c nsnames.c nsobject.c nsparse.c nssearch.c nspredef.c
SRCS+=	nsutils.c nswalk.c nsxfeval.c nsxfname.c nsxfobj.c
SRCS+=	psargs.c psloop.c psopcode.c psparse.c psscope.c
SRCS+=	pstree.c psutils.c pswalk.c psxface.c
SRCS+=	rsaddr.c rscalc.c rscreate.c rsdump.c rsinfo.c rsio.c
SRCS+=	rsirq.c rslist.c rsmemory.c rsmisc.c rsutils.c
SRCS+=	rsxface.c tbfadt.c tbfind.c tbinstal.c
SRCS+=	tbutils.c tbxface.c tbxfroot.c utalloc.c
SRCS+=	utclib.c utcopy.c utdebug.c utdelete.c uteval.c utlock.c
SRCS+=	utglobal.c utinit.c utmath.c utmisc.c utmutex.c utobject.c
SRCS+=	utresrc.c utstate.c utxface.c utdecode.c utids.c utosi.c utxferror.c

# OSD layer
#
SRCS+=  acpi.c acpi_acad.c acpi_battery.c acpi_button.c acpi_cmbat.c
SRCS+=	acpi_cpu.c acpi_cpu_cstate.c acpi_cpu_pstate.c
SRCS+=  acpi_ec.c acpi_isab.c acpi_lid.c
SRCS+=  acpi_package.c
# The PCI part of ACPI5 doesn't work with the current infrastructure
SRCS+=	acpi_pci.c acpi_pcib.c acpi_pcib_acpi.c acpi_pcib_pci.c
SRCS+=	acpi_pci_link.c
SRCS+=  acpi_powerres.c acpi_resource.c acpi_thermal.c
SRCS+=  acpi_timer.c acpi_hpet.c acpi_if.c
SRCS+=  OsdDebug.c
SRCS+=  OsdHardware.c OsdInterface.c OsdInterrupt.c OsdMemory.c OsdSchedule.c
SRCS+=  OsdStream.c OsdSynch.c OsdTable.c OsdEnvironment.c
SRCS+=  opt_acpi.h opt_bus.h opt_ddb.h
SRCS+=  device_if.h bus_if.h pci_if.h pcib_if.h isa_if.h acpi_if.h acpi_wmi_if.h
MFILES = kern/device_if.m kern/bus_if.m bus/pci/pci_if.m bus/pci/pcib_if.m
MFILES+= bus/isa/isa_if.m dev/acpica5/acpi_if.m dev/acpica5/acpi_wmi_if.m

.if ACPI_NO_SEMAPHORES
CFLAGS+=-DACPI_NO_SEMAPHORES
.endif
.if ACPI_DEBUG
CFLAGS+=-DACPI_DEBUG
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

# Machine-specific code for P-State
SRCS+=	acpi_pstate_machdep.c
# Machine-specific code such as sleep/wakeup
SRCS+=	acpi_machdep.c acpi_wakecode.h
SRCS+=	acpi_wakeup.c
.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
# APIC enumerators
#SRCS+=	madt.c
SRCS+=	pmtimer.c
.endif
CLEANFILES+=	acpi_wakecode.h acpi_wakecode.o acpi_wakecode.bin ${DBSRC}
CLEANFILES+=	platform/acenv.h

acpi_wakecode.h: acpi_wakecode.S
	${MAKE} -f ${SYSDIR}/${OSACPI_MD_DIR}/Makefile \
		MAKESRCPATH=${SYSDIR}/${OSACPI_MD_DIR}

SUBDIR=	acpi_asus acpi_fujitsu acpi_hp acpi_panasonic acpi_sony acpi_thinkpad acpi_toshiba acpi_video aibs
all: ${PROG} ${SUBDIR}

.include <bsd.kmod.mk>
