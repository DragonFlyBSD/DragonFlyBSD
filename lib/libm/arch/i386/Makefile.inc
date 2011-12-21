# $DragonFly: src/lib/libm/arch/i386/Makefile.inc,v 1.2 2007/06/16 22:26:53 pavalos Exp $

.PATH:	${.CURDIR}/arch/i386

SRCS+=	e_acos.S e_asin.S e_atan2.S e_atan2f.S e_exp.S e_expf.S e_fmod.S \
	e_log.S e_log10.S e_log10f.S e_log2.S e_log2f.S e_logf.S \
	e_scalb.S e_scalbf.S e_sqrt.S e_sqrtf.S s_lrint.S s_atan.S s_atanf.S \
	s_ceil.S s_ceilf.S s_copysign.S s_copysignf.S s_cos.S s_cosf.S \
	s_finite.S s_finitef.S s_floor.S s_floorf.S s_ilogb.S s_ilogbf.S \
	s_log1p.S s_log1pf.S s_logb.S s_logbf.S s_rint.S s_rintf.S \
	s_scalbn.S s_scalbnf.S s_significand.S s_significandf.S s_sin.S \
	s_sinf.S s_tan.S s_tanf.S e_remainder.S e_remainderf.S
SRCS+=  s_ilogbl.S s_logbl.S s_modf.S s_scalbnl.S
SRCS+=  s_remquol.S e_remainderl.S s_lrintf.S s_lrintl.S

# This file is included by arch/x86_64/Makefile.inc, so pull in the right
# fenv.[ch].
#
.if ${MACHINE_ARCH} == "i386"
SRCS+=	fenv.c
INCS=	fenv.h
.endif
