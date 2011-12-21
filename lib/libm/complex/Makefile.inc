# $NetBSD: Makefile.inc,v 1.2 2008/03/08 14:21:41 drochner Exp $

.PATH: ${.CURDIR}/complex

SRCS+= cabs.c cabsf.c w_cabsl.c
SRCS+= carg.c cargf.c s_cargl.c
SRCS+= creal.c crealf.c creall.c
SRCS+= cimag.c cimagf.c cimagl.c
SRCS+= conj.c conjf.c conjl.c
SRCS+= cproj.c cprojf.c cprojl.c
SRCS+= csqrt.c csqrtf.c s_csqrtl.c
SRCS+= cexp.c cexpf.c
SRCS+= clog.c clogf.c
SRCS+= cpow.c cpowf.c
SRCS+= cephes_subr.c cephes_subrf.c
SRCS+= csin.c csinf.c
SRCS+= ccos.c ccosf.c
SRCS+= ctan.c ctanf.c
SRCS+= csinh.c csinhf.c
SRCS+= ccosh.c ccoshf.c
SRCS+= ctanh.c ctanhf.c
SRCS+= casin.c casinf.c
SRCS+= cacos.c cacosf.c
SRCS+= catan.c catanf.c
SRCS+= casinh.c casinhf.c
SRCS+= cacosh.c cacoshf.c
SRCS+= catanh.c catanhf.c

MAN+= cacos.3 cacosh.3 carg.3 casin.3 casinh.3 catan.3 catanh.3
MAN+= ccos.3 ccosh.3 cexp.3 cimag.3 clog.3 conj.3 cpow.3 cproj.3 creal.3
MAN+= csin.3 csinh.3 csqrt.3 ctan.3 ctanh.3

MLINKS+= cacos.3 cacosf.3 cacosh.3 cacoshf.3
MLINKS+= carg.3 cargf.3 carg.3 cargl.3
MLINKS+= casin.3 casinf.3 casinh.3 casinhf.3
MLINKS+= catan.3 catanf.3 catanh.3 catanhf.3 ccos.3 ccosf.3
MLINKS+= ccosh.3 ccoshf.3 cexp.3 cexpf.3 cimag.3 cimagf.3 cimag.3 cimagl.3
MLINKS+= clog.3 clogf.3 conj.3 conjf.3 conj.3 conjl.3 cpow.3 cpowf.3
MLINKS+= cproj.3 cprojf.3 cproj.3 cprojl.3
MLINKS+= creal.3 crealf.3 creal.3 creall.3 csin.3 csinf.3 csinh.3 csinhf.3
MLINKS+= csqrt.3 csqrtf.3 csqrt.3 csqrtl.3
MLINKS+= ctan.3 ctanf.3 ctanh.3 ctanhf.3
