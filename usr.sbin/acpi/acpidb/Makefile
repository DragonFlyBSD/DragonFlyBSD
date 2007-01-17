# $FreeBSD: src/usr.sbin/acpi/acpidb/Makefile,v 1.5 2004/05/25 02:56:55 njl Exp $
# $DragonFly: src/usr.sbin/acpi/acpidb/Makefile,v 1.4 2007/01/17 17:31:19 y0netan1 Exp $

PROG=	acpidb
SRCS+=	acpidb.c
SRCS+=	osunixxf.c
SRCS+=	dbcmds.c dbdisply.c dbexec.c dbfileio.c \
	dbhistry.c dbinput.c dbstats.c dbutils.c \
	dbxface.c dmbuffer.c dmnames.c dmobject.c \
	dmopcode.c dmresrc.c dmresrcl.c dmresrcs.c \
	dmutils.c dmwalk.c dsfield.c dsinit.c \
	dsmethod.c dsmthdat.c dsobject.c dsopcode.c \
	dsutils.c dswexec.c dswload.c dswscope.c dswstate.c \
	evevent.c evgpe.c evgpeblk.c evmisc.c \
	evregion.c evrgnini.c evsci.c evxface.c \
	evxfevnt.c evxfregn.c exconfig.c exconvrt.c \
	excreate.c exdump.c exfield.c exfldio.c \
	exmisc.c exmutex.c exnames.c exoparg1.c \
	exoparg2.c exoparg3.c exoparg6.c exprep.c \
	exregion.c exresnte.c exresolv.c exresop.c \
	exstore.c exstoren.c exstorob.c exsystem.c exutils.c \
	hwacpi.c hwgpe.c hwregs.c hwsleep.c \
	nsaccess.c nsalloc.c nsdump.c nseval.c \
	nsinit.c nsload.c nsnames.c nsobject.c \
	nsparse.c nssearch.c nsutils.c nswalk.c \
	nsxfeval.c nsxfname.c nsxfobj.c \
	psargs.c psloop.c psopcode.c psparse.c psscope.c \
	pstree.c psutils.c pswalk.c psxface.c \
	rsaddr.c rscalc.c rscreate.c rsdump.c rsinfo.c \
	rsio.c rsirq.c rslist.c rsmemory.c \
	rsmisc.c rsutils.c rsxface.c \
	tbfadt.c tbfind.c tbinstal.c tbutils.c tbxface.c tbxfroot.c \
	utalloc.c utcache.c utcopy.c utdebug.c utdelete.c \
	uteval.c utglobal.c utinit.c utmath.c \
	utmisc.c utmutex.c utobject.c utresrc.c utstate.c utxface.c

SRCS+=	aeexec.c

.include <bsd.init.mk>
MAN=	acpidb.8
.PATH:	${SYSDIR}/${ACPICA_DIR}/tools/acpiexec

CFLAGS+= -DACPI_APPLICATION -DACPI_DEBUG_OUTPUT -DACPI_DEBUGGER \
	-DACPI_DISASSEMBLER -D__cdecl=

CONTRIBDIR=	${SYSDIR}/${ACPICA_DIR}
SRCS+=		osunixxf.c.patch

.include <bsd.prog.mk>
OBJS:=		${OBJS:N*/*}
