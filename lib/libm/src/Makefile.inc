# $DragonFly: src/lib/libm/src/Makefile.inc,v 1.1 2005/07/26 21:15:20 joerg Exp $

.PATH:	${.CURDIR}/src

# This files are always used.
SRCS+=	k_cos.c k_cosf.c k_rem_pio2.c k_rem_pio2f.c k_sin.c k_sinf.c \
	k_tan.c k_tanf.c llrint.c llrintf.c llround.c \
	llroundf.c lrint.c lrintf.c lround.c lroundf.c w_cabs.c w_cabsf.c \
	w_drem.c w_dremf.c w_gamma.c w_gamma_r.c w_gammaf.c w_gammaf_r.c \
	w_lgamma.c w_lgammaf.c

# This files are only used if the MI Makefile didn't already provide
# them as assembler version.

MI_FUNCS+=	\
	e_acos.c e_acosf.c e_acosh.c e_acoshf.c e_asin.c e_asinf.c \
	e_atan2.c e_atan2f.c e_atanh.c e_atanhf.c e_cosh.c e_coshf.c \
	e_exp.c e_expf.c e_fmod.c e_fmodf.c e_hypot.c e_hypotf.c e_j0.c \
	e_j0f.c e_j1.c e_j1f.c e_jn.c e_jnf.c e_lgamma_r.c e_lgammaf_r.c \
	e_log.c e_log10.c e_log10f.c e_logf.c e_pow.c e_powf.c e_rem_pio2.c \
	e_rem_pio2f.c e_remainder.c e_remainderf.c e_scalb.c e_scalbf.c \
	e_sinh.c e_sinhf.c e_sqrt.c e_sqrtf.c \
	s_asinh.c s_asinhf.c s_atan.c s_atanf.c s_cbrt.c s_cbrtf.c s_ceil.c \
	s_ceilf.c s_copysign.c s_copysignf.c s_cos.c s_cosf.c s_erf.c \
	s_erff.c s_expm1.c s_expm1f.c s_fabsf.c s_finite.c \
	s_finitef.c s_floor.c s_floorf.c s_frexpf.c s_ilogb.c \
	s_ilogbf.c s_ldexpf.c s_log1p.c s_log1pf.c s_logb.c s_logbf.c \
	s_modff.c s_nextafter.c s_nextafterf.c s_rint.c s_rintf.c \
	s_round.c s_roundf.c s_scalbn.c s_scalbnf.c \
	s_signgam.c s_significand.c s_significandf.c s_sin.c s_sinf.c \
	s_tan.c s_tanf.c s_tanh.c s_tanhf.c

.for f in ${MI_FUNCS}
.if empty(SRCS:M${f:H:R})
SRCS+=	$f
.endif
.endfor
