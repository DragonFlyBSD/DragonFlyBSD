# $DragonFly: src/lib/libm/src/Makefile.inc,v 1.3 2007/06/17 06:26:18 pavalos Exp $

.PATH:	${.CURDIR}/src

LIBCDIR=${.CURDIR}/../libc
CFLAGS+=-I${LIBCDIR}/include -I${LIBCDIR}/${MACHINE_ARCH}

# These files are always used.
SRCS+=	k_cos.c k_cosf.c k_rem_pio2.c k_rem_pio2f.c k_sin.c k_sinf.c \
	k_tan.c k_tanf.c s_llrint.c s_llrintf.c s_llround.c s_llroundf.c \
	s_lrintf.c s_lround.c s_lroundf.c s_trunc.c s_truncf.c \
	w_drem.c w_dremf.c w_gamma.c w_gamma_r.c w_gammaf.c w_gammaf_r.c \
	w_lgamma.c w_lgammaf.c

# Origin: FreeBSD's msun library, always used source files
# Long Double: 80-bit version used (LD80 on FreeBSD)

SRCS+=	invtrig.c e_acosl.c e_asinl.c e_atan2l.c e_fmodl.c e_hypotl.c \
	k_sinl.c k_cosl.c k_tanl.c b_log.c b_exp.c \
	s_atanl.c s_sinl.c s_cosl.c s_tanl.c s_exp2l.c s_cbrtl.c s_floorl.c \
	s_roundl.c s_lroundl.c s_llroundl.c s_rintl.c s_lrintl.c s_modfl.c \
	s_fabs.c s_fma.c s_fmaf.c s_fmal.c s_tgammaf.c b_tgamma.c s_scalbln.c

# These files are only used if the MI Makefile didn't already provide
# them as assembler version.

MI_FUNCS+=	\
	e_acos.c e_acosf.c e_acosh.c e_acoshf.c e_asin.c e_asinf.c \
	e_atan2.c e_atan2f.c e_atanh.c e_atanhf.c e_cosh.c e_coshf.c \
	e_exp.c e_expf.c e_fmod.c e_fmodf.c e_hypot.c e_hypotf.c e_j0.c \
	e_j0f.c e_j1.c e_j1f.c e_jn.c e_jnf.c e_lgamma_r.c e_lgammaf_r.c \
	e_log.c e_log10.c e_log10f.c e_log2.c e_log2f.c e_logf.c e_pow.c e_powf.c e_rem_pio2.c \
	e_rem_pio2f.c e_remainder.c e_remainderf.c e_scalb.c e_scalbf.c \
	e_sinh.c e_sinhf.c e_sqrt.c e_sqrtf.c \
	s_asinh.c s_asinhf.c s_atan.c s_atanf.c s_cbrt.c s_cbrtf.c s_ceil.c \
	s_ceilf.c s_copysign.c s_copysignf.c s_copysignl.c s_cos.c s_cosf.c s_erf.c \
	s_erff.c s_expm1.c s_expm1f.c s_fabsf.c s_fdim.c s_finite.c \
	s_finitef.c s_floor.c s_floorf.c s_fmax.c s_fmaxf.c s_fmaxl.c \
	s_fmin.c s_fminf.c s_fminl.c s_frexpf.c s_frexpl.c s_ilogb.c \
	s_ilogbf.c s_log1p.c s_log1pf.c s_logb.c s_logbf.c \
	s_modff.c s_nearbyint.c s_nextafter.c s_nextafterf.c s_rint.c s_rintf.c \
	s_round.c s_roundf.c s_scalbn.c s_scalbnf.c \
	s_signgam.c s_significand.c s_significandf.c s_sin.c s_sinf.c \
	s_tan.c s_tanf.c s_tanh.c s_tanhf.c s_lrint.c
MI_FUNCS+= s_exp2.c s_exp2f.c s_frexp.c s_ilogbl.c s_logbl.c s_nextafterl.c \
	s_nexttoward.c s_remquo.c s_remquof.c s_scalbnl.c s_fabsl.c
MI_FUNCS+= e_sqrtl.c e_remainderl.c s_remquol.c s_ceill.c s_llrintl.c \
	s_modf.c s_nexttowardf.c s_truncl.c

.for f in ${MI_FUNCS}
.if empty(SRCS:M${f:H:R})
SRCS+=	$f
.endif
.endfor
