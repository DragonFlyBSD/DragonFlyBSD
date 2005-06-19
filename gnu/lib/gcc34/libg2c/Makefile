# $DragonFly: src/gnu/lib/gcc34/libg2c/Attic/Makefile,v 1.3 2005/06/19 17:13:59 joerg Exp $

.include "../Makefile.inc"

LIB=	g2c
SHLIB_MAJOR=	3

SRCDIR=	${GCCDIR}/libf2c

.PATH: ${SRCDIR} ${SRCDIR}/libF77 ${SRCDIR}/libI77 ${SRCDIR}/libU77

CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${SRCDIR}
CFLAGS+=	-I${SRCDIR}/libF77
CFLAGS+=	-I${SRCDIR}/libI77

# libE77
F2CEXT=	abort derf derfc ef1asc ef1cmc erf erfc exit getarg getenv iargc \
	signal system flush ftell fseek access besj0 besj1 besjn besy0 besy1 \
	besyn chdir chmod ctime dbesj0 dbesj1 dbesjn dbesy0 dbesy1 dbesyn \
	dtime etime fdate fgetc fget flush1 fnum fputc fput fstat gerror \
	getcwd getgid getlog getpid getuid gmtime hostnm idate ierrno irand \
	isatty itime kill link lnblnk lstat ltime mclock perror rand rename \
	secnds second sleep srand stat symlnk time ttynam umask unlink \
	vxttim alarm \
	date_y2kbuggy date_y2kbug vxtidt_y2kbuggy vxtidt_y2kbug

.for symbol in ${F2CEXT}
L${symbol}.c:
	rm -f ${.TARGET}
	echo '#define L${symbol}' > ${.TARGET}
	echo '#include "f2cext.c"' >> ${.TARGET}
SRCS+=	L${symbol}.c
CLEANFILES+=	L${symbol}.c
.endfor

# libF77
SRCS+=	F77_aloc.c VersionF.c s_rnge.c abort_.c getarg_.c iargc_.c
SRCS+=	getenv_.c signal_.c s_stop.c s_paus.c system_.c cabs.c
SRCS+=	derf_.c derfc_.c erf_.c erfc_.c sig_die.c exit_.c setarg.c setsig.c
SRCS+=	pow_ci.c pow_dd.c pow_di.c pow_hh.c pow_ii.c pow_ri.c pow_zi.c
SRCS+=	pow_zz.c pow_qq.c
SRCS+=	c_abs.c c_cos.c c_div.c c_exp.c c_log.c c_sin.c c_sqrt.c
SRCS+=	z_abs.c z_cos.c z_div.c z_exp.c z_log.c z_sin.c z_sqrt.c
SRCS+=	r_abs.c r_acos.c r_asin.c r_atan.c r_atn2.c r_cnjg.c r_cos.c
SRCS+=	r_cosh.c r_dim.c r_exp.c r_imag.c r_int.c
SRCS+=	r_lg10.c r_log.c r_mod.c r_nint.c r_sign.c
SRCS+=	r_sin.c r_sinh.c r_sqrt.c r_tan.c r_tanh.c
SRCS+=	d_abs.c d_acos.c d_asin.c d_atan.c d_atn2.c
SRCS+=	d_cnjg.c d_cos.c d_cosh.c d_dim.c d_exp.c
SRCS+=	d_imag.c d_int.c d_lg10.c d_log.c d_mod.c
SRCS+=	d_nint.c d_prod.c d_sign.c d_sin.c d_sinh.c
SRCS+=	d_sqrt.c d_tan.c d_tanh.c
SRCS+=	i_abs.c i_dim.c i_dnnt.c i_indx.c i_len.c i_mod.c i_nint.c i_sign.c
SRCS+=	h_abs.c h_dim.c h_dnnt.c h_indx.c h_len.c h_mod.c h_nint.c h_sign.c
SRCS+=	l_ge.c l_gt.c l_le.c l_lt.c hl_ge.c hl_gt.c hl_le.c hl_lt.c
SRCS+=	ef1asc_.c ef1cmc_.c
SRCS+=	s_cat.c s_cmp.c s_copy.c
SRCS+=	lbitbits.c lbitshft.c qbitbits.c qbitshft.c
SRCS+=	mainF.c
# libI77
SRCS+=	VersionI.c backspace.c close.c dfe.c dolio.c due.c endfile.c err.c
SRCS+=	fmt.c fmtlib.c iio.c ilnw.c inquire.c lread.c lwrite.c open.c
SRCS+=	rdfmt.c rewind.c rsfe.c rsli.c rsne.c sfe.c sue.c typesize.c uio.c
SRCS+=	util.c wref.c wrtfmt.c wsfe.c wsle.c wsne.c xwsne.c ftell_.c
# libU77
SRCS+=	VersionU.c gerror_.c perror_.c ierrno_.c itime_.c time_.c
SRCS+=	unlink_.c fnum_.c getpid_.c getuid_.c getgid_.c kill_.c rand_.c
SRCS+=	srand_.c irand_.c sleep_.c idate_.c ctime_.c etime_.c
SRCS+=	dtime_.c isatty_.c ltime_.c fstat_.c stat_.c
SRCS+=	lstat_.c access_.c link_.c getlog_.c ttynam_.c getcwd_.c symlnk_.c
SRCS+=	vxttime_.c vxtidate_.c gmtime_.c fdate_.c secnds_.c
SRCS+=	chdir_.c chmod_.c lnblnk_.c hostnm_.c rename_.c fgetc_.c fputc_.c
SRCS+=	umask_.c sys_clock_.c date_.c second_.c flush1_.c mclock_.c
SRCS+=	alarm_.c datetime_.c

SRCS+=	g2c.h

g2c.h: ${SRCDIR}/g2c.hin
	cp ${SRCDIR}/g2c.hin ${.TARGET}
mainF.c: ${SRCDIR}/libF77/main.c
	cp ${SRCDIR}/libF77/main.c ${.TARGET}
VersionF.c: ${SRCDIR}/libF77/Version.c
	cp ${SRCDIR}/libF77/Version.c ${.TARGET}
VersionI.c: ${SRCDIR}/libI77/Version.c
	cp ${SRCDIR}/libI77/Version.c ${.TARGET}
VersionU.c: ${SRCDIR}/libU77/Version.c
	cp ${SRCDIR}/libU77/Version.c ${.TARGET}

CLEANFILES+=	g2c.h mainF.c VersionF.c VersionI.c VersionU.c

.include <bsd.lib.mk>
