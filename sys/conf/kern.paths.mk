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
