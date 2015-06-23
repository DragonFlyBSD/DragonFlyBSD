# Set up the default install paths for kernel/modules
#
# DESTLABEL		add suffix to kernel and modules directory [not set]
# DESTKERNDIR		where kernel and modules are to be installed [/boot]
# DESTKERNNAME		name of installed kernel [${KERNEL}]
# OLDMODULESDIR		(deprecated)
#
.if defined(DESTLABEL)
DESTKERNDIR?=		/boot/${KERNEL}.${DESTLABEL}
DESTKERNNAME?=		kernel
OLDMODULESDIR?=		/boot/modules.${DESTLABEL}
.else
DESTKERNDIR?=		/boot/${KERNEL}
DESTKERNNAME?=		kernel
OLDMODULESDIR?=		/boot/modules
.endif

# Set DESTDIR to /var/vkernel by default for vkernel platform so as
# not to shoot the real kernel installation.
.if ${MACHINE_PLATFORM} == vkernel64
DESTDIR?=		/var/vkernel
.endif
