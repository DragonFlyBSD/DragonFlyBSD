# Set up the default install paths for kernel/modules
#
# DESTLABEL		add suffix to kernel and modules directory [not set]
# DESTKERNDIR		where kernel and modules are to be installed [/boot]
# DESTKERNNAME		name of installed kernel [${KERNEL}]
# DESTMODULESNAME	name of modules directory [modules]
#
.if defined(DESTLABEL)
DESTKERNNAME?=		${KERNEL}.${DESTLABEL}
DESTMODULESNAME?=	modules.${DESTLABEL}
.else
DESTKERNNAME?=		${KERNEL}
DESTMODULESNAME?=	modules
.endif
DESTKERNDIR?=		/boot

# Set DESTDIR to /var/vkernel by default for vkernel platform so as
# not to shoot the real kernel installation.
.if ${MACHINE_PLATFORM} == vkernel || ${MACHINE_PLATFORM} == vkernel64
DESTDIR?=		/var/vkernel
.endif
