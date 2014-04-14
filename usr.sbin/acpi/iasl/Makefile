#
# iASL compiler/disassembler
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/compiler

.PATH:	${UTILDIR}

PROG=	iasl
MAN=	iasl.8

SRCS=	\
	aslcompiler.y.h \
	aslcompilerlex.c \
	aslcompilerparse.c \
	dtparser.y.h \
	dtparserlex.c \
	dtparserparse.c \
	prparser.y.h \
	prparserlex.c \
	prparserparse.c \
	adfile.c \
	adisasm.c \
	adwalk.c \
	ahids.c \
	ahpredef.c \
	aslanalyze.c \
	aslbtypes.c \
	aslcodegen.c \
	aslcompile.c \
	aslerror.c \
	aslfiles.c \
	aslfileio.c \
	aslfold.c \
	aslhex.c \
	asllength.c \
	asllisting.c \
	asllistsup.c \
	aslload.c \
	asllookup.c \
	aslmain.c \
	aslmap.c \
	aslmethod.c \
	aslnamesp.c \
	asloffset.c \
	aslopcodes.c \
	asloperands.c \
	aslopt.c \
	asloptions.c \
	aslpredef.c \
	aslprepkg.c \
	aslresource.c \
	aslrestype1.c \
	aslrestype1i.c \
	aslrestype2.c \
	aslrestype2d.c \
	aslrestype2e.c \
	aslrestype2q.c \
	aslrestype2s.c \
	aslrestype2w.c \
	aslstartup.c \
	aslstubs.c \
	asltransform.c \
	asltree.c \
	aslutils.c \
	asluuid.c \
	aslwalks.c \
	aslxref.c \
	cmfsize.c \
	dtcompile.c \
	dtexpress.c \
	dtfield.c \
	dtio.c \
	dtsubtable.c \
	dttable.c \
	dttemplate.c \
	dtutils.c \
	dbfileio.c \
	dmbuffer.c \
	dmdeferred.c \
	dmextern.c \
	dmnames.c \
	dmobject.c \
	dmopcode.c \
	dmresrc.c \
	dmresrcl.c \
	dmresrcl2.c \
	dmresrcs.c \
	dmrestag.c \
	dmtable.c \
	dmtbdump.c \
	dmtbinfo.c \
	dmutils.c \
	dmwalk.c \
	dsargs.c \
	dscontrol.c \
	dsfield.c \
	dsobject.c \
	dsopcode.c \
	dsutils.c \
	dswexec.c \
	dswload.c \
	dswload2.c \
	dswscope.c \
	dswstate.c \
	exconvrt.c \
	excreate.c \
	exdump.c \
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
	exstore.c \
	exstoren.c \
	exstorob.c \
	exsystem.c \
	exutils.c \
	getopt.c \
	nsaccess.c \
	nsalloc.c \
	nsdump.c \
	nsnames.c \
	nsobject.c \
	nsparse.c \
	nssearch.c \
	nsutils.c \
	nswalk.c \
	nsxfobj.c \
	osunixxf.c \
	prexpress.c \
	prmacros.c \
	prscan.c \
	prutils.c \
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
	tbdata.c \
	tbfadt.c \
	tbinstal.c \
	tbprint.c \
	tbutils.c \
	tbxface.c \
	utaddress.c \
	utalloc.c \
	utbuffer.c \
	utcache.c \
	utcopy.c \
	utdebug.c \
	utdecode.c \
	utdelete.c \
	uterror.c \
	utexcep.c \
	utglobal.c \
	utinit.c \
	utlock.c \
	utmath.c \
	utmisc.c \
	utmutex.c \
	utobject.c \
	utownerid.c \
	utpredef.c \
	utresrc.c \
	utstate.c \
	utstring.c \
	utxface.c \
	utxferror.c

CFLAGS+= \
	-DACPI_ASL_COMPILER \
	-I${UTILDIR} \
	-I${.CURDIR}

LFLAGS=	-i -s
YFLAGS=	-d

aslcompilerlex.c: aslcompiler.l
	${LEX} ${LFLAGS} -PAslCompiler -o${.TARGET} ${.ALLSRC}

aslcompilerparse.c: aslcompiler.y
	${YACC} ${YFLAGS} -pAslCompiler -o${.TARGET} ${.ALLSRC}

aslcompiler.y.h: aslcompilerparse.c
	mv -f aslcompilerparse.h ${.TARGET}

dtparserlex.c: dtparser.l
	${LEX} ${LFLAGS} -PDtParser -o${.TARGET} ${.ALLSRC}

dtparserparse.c: dtparser.y
	${YACC} ${YFLAGS} -pDtParser -o${.TARGET} ${.ALLSRC}

dtparser.y.h: dtparserparse.c
	mv -f dtparserparse.h ${.TARGET}

prparserlex.c: prparser.l
	${LEX} ${LFLAGS} -PPrParser -o${.TARGET} ${.ALLSRC}

prparserparse.c: prparser.y
	${YACC} ${YFLAGS} -pPrParser -o${.TARGET} ${.ALLSRC}

prparser.y.h: prparserparse.c
	mv -f prparserparse.h ${.TARGET}

CLEANFILES+=	\
		aslcompilerlex.c \
		aslcompilerparse.c \
		aslcompiler.y.h \
		dtparserlex.c \
		dtparserparse.c \
		dtparser.y.h \
		prparserlex.c \
		prparserparse.c \
		prparser.y.h

.include <bsd.prog.mk>
