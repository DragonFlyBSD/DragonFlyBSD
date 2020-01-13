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
	acfileio.c \
	anmain.c \
	anstubs.c \
	antables.c

SRCS+=	\
	cmfsize.c \
	dscontrol.c \
	dsdebug.c \
	dsfield.c \
	dsinit.c \
	dsmethod.c \
	dsmthdat.c \
	dsobject.c \
	dsopcode.c \
	dspkginit.c \
	dsutils.c \
	dswexec.c \
	dswload.c \
	dswload2.c \
	dswscope.c \
	dswstate.c \
	evhandler.c \
	evmisc.c \
	evregion.c \
	evrgnini.c \
	evxfregn.c \
	exconcat.c \
	exconfig.c \
	exconvrt.c \
	excreate.c \
	exdump.c \
	exfield.c \
	exfldio.c \
	exmisc.c \
	exmutex.c \
	exnames.c \
	exoparg1.c \
	exoparg2.c \
	exoparg3.c \
	exoparg6.c \
	exprep.c \
	exregion.c \
	exresnte.c \
	exresolv.c \
	exresop.c \
	exserial.c \
	exstore.c \
	exstoren.c \
	exstorob.c \
	exsystem.c \
	exutils.c \
	getopt.c \
	hwpci.c \
	hwvalid.c \
	nsaccess.c \
	nsalloc.c \
	nsarguments.c \
	nsconvert.c \
	nsdump.c \
	nseval.c \
	nsinit.c \
	nsload.c \
	nsnames.c \
	nsobject.c \
	nsparse.c \
	nspredef.c \
	nsprepkg.c \
	nsrepair.c \
	nsrepair2.c \
	nssearch.c \
	nsutils.c \
	nswalk.c \
	nsxfeval.c \
	nsxfname.c \
	nsxfobj.c \
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
	utascii.c \
	utbuffer.c \
	utcache.c \
	utcopy.c \
	utdebug.c \
	utdecode.c \
	utdelete.c \
	uterror.c \
	uteval.c \
	utexcep.c \
	utglobal.c \
	uthex.c \
	utids.c \
	utinit.c \
	utlock.c \
	utmath.c \
	utmisc.c \
	utmutex.c \
	utnonansi.c \
	utobject.c \
	utosi.c \
	utownerid.c \
	utpredef.c \
	utresrc.c \
	utstate.c \
	utstring.c \
	utstrsuppt.c \
	utstrtoul64.c \
	utxface.c \
	utxferror.c \
	utxfinit.c

CFLAGS+= \
	-DACPI_NAMES_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
