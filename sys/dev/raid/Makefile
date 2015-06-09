SUBDIR=	aac amr arcmsr asr ciss hpt27xx hptiop hptmv hptrr \
	iir ips mfi mlx mly mps mrsas twa twe tws vinum

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=pst
.endif

# Empty for concurrent build
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
