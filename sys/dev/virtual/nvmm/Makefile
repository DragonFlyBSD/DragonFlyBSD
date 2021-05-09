KMOD=	nvmm
SRCS=	nvmm.c

.if ${MACHINE_ARCH} == "x86_64"
.PATH:	${.CURDIR}/x86
SRCS+=	nvmm_x86.c
SRCS+=	nvmm_x86_svm.c nvmm_x86_svmfunc.S
SRCS+=	nvmm_x86_vmx.c nvmm_x86_vmxfunc.S
.endif

# Header files are installed by 'include/Makefile'.

.include <bsd.kmod.mk>
