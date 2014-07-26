#
# acpinames - Load ACPI table and dump namespace. This is a subset
# of the AcpiExec functionality, it is intended to demonstrate
# the configurability of ACPICA.
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpinames

.PATH:	${UTILDIR}

PROG=	acpinames
MAN=	acpinames.8

SRCS=	\
	anmain.c \
	anstubs.c \
	antables.c

SRCS+=	\
	cmfsize.c \
	dsfield.c \
	dsmthdat.c \
	dsobject.c \
	dsutils.c \
	dswload.c \
	dswload2.c \
	dswscope.c \
	dswstate.c \
	excreate.c \
	exdump.c \
	exmutex.c \
	exnames.c \
	exresnte.c \
	exresolv.c \
	exsystem.c \
	exutils.c \
	getopt.c \
	nsaccess.c \
	nsalloc.c \
	nsdump.c \
	nsinit.c \
	nsload.c \
	nsnames.c \
	nsobject.c \
	nsparse.c \
	nssearch.c \
	nsutils.c \
	nswalk.c \
	nsxfeval.c \
	nsxfname.c \
	nsxfobj.c \
	oslibcfs.c \
	osunixxf.c \
	psargs.c \
	psloop.c \
	psobject.c \
	psopcode.c \
	psopinfo.c \
	psparse.c \
	psscope.c \
	pstree.c \
	psutils.c \
	pswalk.c \
	psxface.c \
	tbdata.c \
	tbfadt.c \
	tbfind.c \
	tbinstal.c \
	tbprint.c \
	tbutils.c \
	tbxface.c \
	tbxfload.c \
	tbxfroot.c \
	utaddress.c \
	utalloc.c \
	utbuffer.c \
	utcache.c \
	utdebug.c \
	utdecode.c \
	utdelete.c \
	uterror.c \
	uteval.c \
	utexcep.c \
	utfileio.c \
	utglobal.c \
	uthex.c \
	utids.c \
	utinit.c \
	utlock.c \
	utmath.c \
	utmisc.c \
	utmutex.c \
	utobject.c \
	utosi.c \
	utownerid.c \
	utprint.c \
	utstate.c \
	utstring.c \
	utxface.c \
	utxferror.c \
	utxfinit.c

CFLAGS+= \
	-DACPI_NAMES_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
