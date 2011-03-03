# $FreeBSD: src/usr.sbin/acpi/iasl/Makefile,v 1.5 2004/01/13 20:53:56 ru Exp $
# $DragonFly: src/usr.sbin/acpi/iasl/Makefile,v 1.2 2007/01/17 17:31:20 y0netan1 Exp $

PROG=	iasl
SRCS=	aslcompiler.y.h aslcompilerlex.l aslcompilerparse.y \
	aslanalyze.c aslbtypes.c aslwalks.c aslcodegen.c \
	aslcompile.c aslerror.c aslfiles.c asllength.c \
	asllisting.c aslload.c asllookup.c aslmain.c \
	aslmap.c aslopcodes.c asloperands.c aslstartup.c aslresource.c \
	aslrestype1.c aslrestype2.c asltree.c aslutils.c \
	asltransform.c aslfold.c aslstubs.c aslopt.c \
	aslpredef.c asluuid.c aslrestype1i.c aslrestype2d.c aslrestype2e.c \
	aslrestype2q.c aslrestype2w.c

SRCS+=	adisasm.c getopt.c osunixxf.c
SRCS+=	dbfileio.c dmbuffer.c dmnames.c dmopcode.c dmobject.c \
        dmresrc.c dmresrcl.c dmresrcs.c dmutils.c dmwalk.c dmextern.c \
        dsopcode.c dsargs.c dscontrol.c dsutils.c dswexec.c dswload.c \
	dswscope.c dswstate.c dsfield.c dsobject.c dswload2.c \
        exconvrt.c excreate.c exdump.c exmisc.c \
        exmutex.c exnames.c exoparg1.c exoparg2.c \
        exoparg3.c exoparg6.c exprep.c exregion.c \
        exresnte.c exresolv.c exresop.c exstore.c \
        exstoren.c exstorob.c exsystem.c exutils.c \
        nsaccess.c nsalloc.c nsdump.c nsnames.c nsobject.c \
	nsparse.c nssearch.c nsutils.c nswalk.c nsxfobj.c \
        psargs.c psopcode.c psparse.c psscope.c \
        pstree.c psutils.c pswalk.c \
        tbfadt.c tbinstal.c tbutils.c tbxface.c \
	utalloc.c utcopy.c utdebug.c utdelete.c utinit.c \
        utglobal.c utobject.c utlock.c utmisc.c utmath.c utdecode.c \
		utxface.c utxferror.c \
	dttemplate.c dtcompile.c dtsubtable.c dtutils.c dtfield.c dtio.c dttable.c dtexpress.c

SRCS+=	adfile.c adwalk.c dmrestag.c dmtbdump.c dmtbinfo.c dmtable.c psloop.c utcache.c utmutex.c utresrc.c utstate.c

MAN=	iasl.8

CFLAGS+= -D_ACPI_ASL_COMPILER -DACPI_ASL_COMPILER -DACPI_USE_ALTERNATE_TIMEOUT -I.

CFLAGS+= -D_USE_BERKELEY_YACC
LDFLAGS+= -lpthread
LFLAGS=	-i -PAslCompiler
YFLAGS=	-d -pAslCompiler

CLEANFILES= aslcompiler.y.h aslcompilerlex.l aslcompilerparse.y

aslcompiler.y.h: aslcompilerparse.h
	cat ${.ALLSRC} > ${.TARGET}

aslcompilerlex.l: aslcompiler.l
	cat ${.ALLSRC} > ${.TARGET}

aslcompilerparse.y: aslcompiler.y
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
