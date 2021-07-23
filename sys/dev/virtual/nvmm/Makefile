KMOD=	nvmm
SRCS=	nvmm.c nvmm_dragonfly.c

.if ${MACHINE_ARCH} == "x86_64"
.PATH:	${.CURDIR}/x86
SRCS+=	nvmm_x86.c
SRCS+=	nvmm_x86_svm.c nvmm_x86_svmfunc.S
SRCS+=	nvmm_x86_vmx.c nvmm_x86_vmxfunc.S

nvmm_x86_svmfunc.o nvmm_x86_vmxfunc.o:
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS:N-flto} ${.IMPSRC}

.if !defined(BUILDING_WITH_KERNEL)
SRCS+=	bus_if.h device_if.h opt_nfs.h  # required by 'genassym.c'
SRCS+=	assym.s
nvmm_x86_svmfunc.o nvmm_x86_vmxfunc.o: assym.s
.endif # !BUILDING_WITH_KERNEL
.endif

# Header files are installed by 'include/Makefile'.

.include <bsd.kmod.mk>
