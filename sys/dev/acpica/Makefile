# $FreeBSD: src/sys/modules/acpi/acpi/Makefile,v 1.3 2004/01/08 16:38:32 njl Exp $

CONTRIBDIR=	${SYSDIR}/${ACPICA_DIR}

SYSDIR?= ${.CURDIR}/../..
.if ACPI_DEBUG_LOCKS || ACPI_DEBUG_MEMMAP || ACPI_DEBUG_CACHE
CFLAGS+=	-I${.OBJDIR}/include
.if ACPI_DEBUG_LOCKS
CFLAGS+=	-DACPI_DEBUG_LOCKS
.endif
.if ACPI_DEBUG_MEMMAP
CFLAGS+=	-DACPI_DEBUG_MEMMAP
.endif
.if ACPI_DEBUG_CACHE
CFLAGS+=	-DACPI_DEBUG_CACHE
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
SRCS+=	dbcmds.c dbconvert.c dbdisply.c dbexec.c dbfileio.c dbhistry.c
SRCS+=	dbinput.c dbstats.c dbutils.c dbxface.c
SRCS+=	dbmethod.c dbnames.c
SRCS+=	dmbuffer.c dmdeferred.c dmnames.c dmobject.c dmopcode.c
SRCS+=	dmresrc.c dmresrcl.c dmresrcl2.c dmresrcs.c dmutils.c dmwalk.c
SRCS+=	dsfield.c dsinit.c dsmethod.c dsmthdat.c
SRCS+=	dsobject.c dsopcode.c dsutils.c dswexec.c dswload.c
SRCS+=	dswscope.c dswstate.c dsargs.c dscontrol.c dswload2.c
SRCS+=	evevent.c evglock.c evgpe.c evgpeblk.c evgpeinit.c evgpeutil.c
SRCS+=	evhandler.c evmisc.c evregion.c evrgnini.c evsci.c evxface.c evxfgpe.c
SRCS+=	evxfevnt.c evxfregn.c exconfig.c exconvrt.c excreate.c
SRCS+=	exdump.c exfield.c exfldio.c exmisc.c exmutex.c
SRCS+=	exnames.c exoparg1.c exoparg2.c exoparg3.c exoparg6.c
SRCS+=	exprep.c exregion.c exresnte.c exresolv.c exresop.c
SRCS+=	exstore.c exstoren.c exstorob.c exsystem.c exutils.c exdebug.c
SRCS+=	hwacpi.c hwesleep.c hwgpe.c hwregs.c hwsleep.c
SRCS+=	hwtimer.c hwvalid.c hwxface.c hwpci.c hwxfsleep.c
SRCS+=	nsarguments.c nsaccess.c nsalloc.c nsconvert.c nsdump.c nseval.c
SRCS+=	nsinit.c nsload.c nsnames.c nsobject.c nsparse.c nspredef.c nsprepkg.c
SRCS+=	nsrepair.c nsrepair2.c nssearch.c
SRCS+=	nsutils.c nswalk.c nsxfeval.c nsxfname.c nsxfobj.c
SRCS+=	psargs.c psloop.c psobject.c psopcode.c psopinfo.c psparse.c psscope.c
SRCS+=	pstree.c psutils.c pswalk.c psxface.c
SRCS+=	rsaddr.c rscalc.c rscreate.c rsdump.c rsdumpinfo.c rsinfo.c rsio.c
SRCS+=	rsirq.c rslist.c rsmemory.c rsmisc.c rsserial.c rsutils.c
SRCS+=	rsxface.c tbfadt.c tbfind.c tbinstal.c tbprint.c
SRCS+=	tbutils.c tbxface.c tbxfload.c tbxfroot.c utalloc.c utbuffer.c
SRCS+=	utaddress.c utclib.c utcopy.c
SRCS+=	utdebug.c utdelete.c uterror.c uteval.c utexcep.c
SRCS+=	utlock.c utglobal.c utinit.c utmath.c utmisc.c utmutex.c
SRCS+=	utobject.c utownerid.c utpredef.c
SRCS+=	utresrc.c utstate.c utstring.c utxface.c utdecode.c utids.c utosi.c
SRCS+=	utxferror.c utxfinit.c

# OSD layer
#
SRCS+=  acpi.c acpi_acad.c acpi_battery.c acpi_button.c acpi_cmbat.c
SRCS+=	acpi_cpu.c acpi_cpu_cstate.c acpi_cpu_pstate.c acpi_smbat.c
SRCS+=  acpi_ec.c acpi_isab.c acpi_lid.c
SRCS+=  acpi_package.c
# The PCI part of ACPI5 doesn't work with the current infrastructure
SRCS+=	acpi_pci.c acpi_pcib.c acpi_pcib_acpi.c acpi_pcib_pci.c
SRCS+=	acpi_pci_link.c
SRCS+=  acpi_powerres.c acpi_resource.c acpi_thermal.c
SRCS+=  acpi_timer.c acpi_hpet.c acpi_if.c acpi_wmi_if.c
SRCS+=  OsdDebug.c
SRCS+=  OsdHardware.c OsdInterrupt.c OsdMemory.c OsdSchedule.c
SRCS+=  OsdStream.c OsdSynch.c OsdTable.c OsdEnvironment.c
SRCS+=  opt_acpi.h opt_ddb.h
SRCS+=  device_if.h bus_if.h pci_if.h pcib_if.h isa_if.h acpi_if.h acpi_wmi_if.h

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
# Machine-specific code for C-State
SRCS+=	acpi_cstate_machdep.c
# Machine-specific code for CPU
SRCS+=	acpi_cpu_machdep.c
# Machine-specific code such as sleep/wakeup
SRCS+=	acpi_machdep.c acpi_wakecode.h
SRCS+=	acpi_wakeup.c
.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
# APIC enumerators
#SRCS+=	madt.c
SRCS+=	pmtimer.c
.endif
CLEANFILES+=	acpi_wakecode.h acpi_wakecode.o acpi_wakecode.bin ${DBSRC}

acpi_wakecode.h: acpi_wakecode.S
	${MAKE} -f ${SYSDIR}/${OSACPI_MD_DIR}/Makefile \
		MAKESRCPATH=${SYSDIR}/${OSACPI_MD_DIR}

SUBDIR=	acpi_asus acpi_fujitsu acpi_hp acpi_panasonic acpi_sony
SUBDIR+=acpi_thinkpad acpi_toshiba acpi_video acpi_wmi aibs

.include <bsd.kmod.mk>
