#	From: @(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91
# $FreeBSD: src/sys/conf/kmod.mk,v 1.82.2.15 2003/02/10 13:11:50 nyan Exp $
#
# The include file <bsd.kmod.mk> handles installing Kernel Loadable Device
# drivers (KLD's).
#
#
# +++ variables +++
# CLEANFILES	Additional files to remove for the clean and cleandir targets.
#
# KMOD          The name of the kernel module to build.
#
# KMODDIR	Base path for kernel modules (see kld(4)).
#		[${DESTKERNDIR}]
#
# KMODOWN	KLD owner. [${BINOWN}]
#
# KMODGRP	KLD group. [${BINGRP}]
#
# KMODMODE	KLD mode. [${BINMODE}]
#
# KMODLOAD	Command to load a kernel module [/sbin/kldload]
#
# KMODUNLOAD	Command to unload a kernel module [/sbin/kldunload]
#
# PROG          The name of the kernel module to build.
#		If not supplied, ${KMOD}.o is used.
#
# SRCS          List of source files
#
# DESTKERNDIR	Change the tree where the kernel and the modules get
#		installed. [/boot]  ${DESTDIR} changes the root of the tree
#		pointed to by ${DESTKERNDIR}.
#
# MFILES	Optionally a list of interfaces used by the module.
#		This file contains a default list of interfaces.
#
# FIRMWS	List of firmware images in format filename:shortname:version
#
# FIRMWARE_LICENSE
#		Set to the name of the license the user has to agree on in
#		order to use this firmware. See /usr/share/doc/legal
#
# +++ targets +++
#
# 	install:
#               install the kernel module and its manual pages; if the Makefile
#               does not itself define the target install, the targets
#               beforeinstall and afterinstall may also be used to cause
#               actions immediately before and after the install target
#		is executed.
#
# 	load:
#		Load KLD.
#
# 	unload:
#		Unload KLD.
#
# bsd.obj.mk: clean, cleandir and obj
# bsd.dep.mk: cleandepend, depend and tags
#

OBJCOPY?=	objcopy
KMODLOAD?=	/sbin/kldload
KMODUNLOAD?=	/sbin/kldunload

# KERNEL is needed when running make install directly from
# the obj directory.
KERNEL?=	kernel

KMODDIR?=	${DESTKERNDIR}
KMODOWN?=	${BINOWN}
KMODGRP?=	${BINGRP}
KMODMODE?=	${BINMODE}

.include <bsd.init.mk>

.SUFFIXES: .out .o .c .cc .cxx .C .y .l .s .S

CFLAGS+=	${COPTS} -D_KERNEL ${CWARNFLAGS}
CFLAGS+=	-DKLD_MODULE

# Don't use any standard include directories.
# Since -nostdinc will annull any previous -I paths, we repeat all
# such paths after -nostdinc.  It doesn't seem to be possible to
# add to the front of `make' variable.
#
# Don't use -I- anymore, source-relative includes are desireable.
_ICFLAGS:=	${CFLAGS:M-I*}
CFLAGS+=	-nostdinc ${_ICFLAGS}

# Add -I paths for system headers.  Individual KLD makefiles don't
# need any -I paths for this.  Similar defaults for .PATH can't be
# set because there are no standard paths for non-headers.
#
# NOTE!  Traditional architecture paths such as <i386/i386/blah.h>
# must run through the "machine_base" softlink using 
# <machine_base/i386/blah.h>.  An explicit cross-architecture path must
# operate relative to /usr/src/sys using e.g. <arch/i386/i386/blah.h>
#
CFLAGS+=	-I. -I@

# Add -I paths for headers in the kernel build directory
#
.if defined(BUILDING_WITH_KERNEL)
CFLAGS+=	-I${BUILDING_WITH_KERNEL}
_MACHINE_FWD=	${BUILDING_WITH_KERNEL}
.else
.if defined(MAKEOBJDIRPREFIX)
_MACHINE_FWD=	${MAKEOBJDIRPREFIX}/${SYSDIR}/forwarder_${MACHINE_ARCH}
.else
_MACHINE_FWD=	${.OBJDIR}/forwarder_${MACHINE_ARCH}
CLEANDIRS+=	${_MACHINE_FWD}
.endif
.endif
CFLAGS+=	-I${_MACHINE_FWD}/include
.include "kern.fwd.mk"

# Add a -I path to standard headers like <stddef.h>.  Use a relative
# path to src/include if possible.  If the @ symlink hasn't been built
# yet, then we can't tell if the relative path exists.  Add both the
# potential relative path and an absolute path in that case.
.if exists(@)
.if exists(@/../include)
CFLAGS+=	-I@/../include
.else
CFLAGS+=	-I${DESTDIR}/usr/include
.endif
.else # !@
CFLAGS+=	-I@/../include -I${DESTDIR}/usr/include
.endif # @

.if defined(BUILDING_WITH_KERNEL) && \
    exists(${BUILDING_WITH_KERNEL}/opt_global.h)
CFLAGS+=	-include ${BUILDING_WITH_KERNEL}/opt_global.h
.endif

CFLAGS+=	${DEBUG_FLAGS}
.if ${MACHINE_ARCH} == x86_64
CFLAGS+=	-fno-omit-frame-pointer
.endif

.include <bsd.patch.mk>

.if defined(FIRMWS)
AWK=/usr/bin/awk
.if !exists(@)
${KMOD:S/$/.c/}: @
.else
${KMOD:S/$/.c/}: @/tools/fw_stub.awk
.endif
	${AWK} -f @/tools/fw_stub.awk ${FIRMWS} -m${KMOD} -c${KMOD:S/$/.c/g} \
	    ${FIRMWARE_LICENSE:C/.+/-l/}${FIRMWARE_LICENSE}

SRCS+=	${KMOD:S/$/.c/}
CLEANFILES+=	${KMOD:S/$/.c/}

.for _firmw in ${FIRMWS}
${_firmw:C/\:.*$/.fwo/}:	${_firmw:C/\:.*$//}
	@${ECHO} ${_firmw:C/\:.*$//} ${.ALLSRC:M*${_firmw:C/\:.*$//}}
	@if [ -e ${_firmw:C/\:.*$//} ]; then			\
		${LD} -b binary --no-warn-mismatch ${LDFLAGS}	\
		    -r -d -o ${.TARGET}	${_firmw:C/\:.*$//};	\
	else							\
		ln -s ${.ALLSRC:M*${_firmw:C/\:.*$//}} ${_firmw:C/\:.*$//}; \
		${LD} -b binary --no-warn-mismatch ${LDFLAGS}	\
		    -r -d -o ${.TARGET}	${_firmw:C/\:.*$//};	\
		rm ${_firmw:C/\:.*$//};				\
	fi

OBJS+=	${_firmw:C/\:.*$/.fwo/}
.endfor
.endif

OBJS+=  ${SRCS:N*.h:N*.patch:R:S/$/.o/g}

.if !defined(PROG)
PROG=	${KMOD}.ko
.endif

.if ${MACHINE_ARCH} != x86_64
${PROG}: ${KMOD}.kld
	${LD} -Bshareable ${LDFLAGS} -o ${.TARGET} ${KMOD}.kld
.endif

.if ${MACHINE_ARCH} != x86_64
${KMOD}.kld: ${OBJS}
	${LD} ${LDFLAGS} -r -o ${.TARGET} ${OBJS}
.else
${PROG}: ${OBJS}
	${LD} ${LDFLAGS} -r -d -o ${.TARGET} ${OBJS}
.endif

# links to platform and cpu architecture include files.  If we are
# building with a kernel these already exist in the kernel build dir.
# '@' is a link to the system source.
.if defined(BUILDING_WITH_KERNEL)
_ILINKS=@
.else
_ILINKS=@ machine_base machine cpu_base cpu
.endif

.if defined(ARCH)
_ILINKS+=${ARCH}
.endif

all: objwarn fwheaders ${PROG}

beforedepend: fwheaders
fwheaders: ${_ILINKS} ${FORWARD_HEADERS_COOKIE}
# Ensure that the links exist without depending on it when it exists which
# causes all the modules to be rebuilt when the directory pointed to changes.
.for _link in ${_ILINKS}
.if !exists(${.OBJDIR}/${_link})
${OBJS}: ${_link}
.endif
.endfor

# Search for kernel source tree in standard places.
.for _dir in ${.CURDIR}/../.. ${.CURDIR}/../../.. ${.CURDIR}/../../../.. /sys /usr/src/sys
.if !defined(SYSDIR) && exists(${_dir}/kern/)
SYSDIR=	${_dir}
.endif
.endfor
.if !defined(SYSDIR) || !exists(${SYSDIR}/kern)
.error "can't find kernel source tree"
.endif
S=	${SYSDIR}

#	path=`(cd $$path && /bin/pwd)` ; 

${_ILINKS}:
	@case ${.TARGET} in \
	machine) \
		path=${SYSDIR}/platform/${MACHINE_PLATFORM}/include ;; \
	machine_base) \
		path=${SYSDIR}/platform/${MACHINE_PLATFORM} ;; \
	cpu) \
		path=${SYSDIR}/cpu/${MACHINE_ARCH}/include ;; \
	cpu_base) \
		path=${SYSDIR}/cpu/${MACHINE_ARCH} ;; \
	@) \
		path=${SYSDIR} ;; \
	arch_*) \
		path=${.CURDIR}/${MACHINE_ARCH} ;; \
	esac ; \
	${ECHO} ${.TARGET} "->" $$path ; \
	${LN} -s $$path ${.TARGET}

CLEANFILES+= ${PROG} ${KMOD}.kld ${OBJS} ${_ILINKS} symb.tmp tmp.o

.if !target(install)

_INSTALLFLAGS:=	${INSTALLFLAGS}
.for ie in ${INSTALLFLAGS_EDIT}
_INSTALLFLAGS:=	${_INSTALLFLAGS${ie}}
.endfor

.if !target(realinstall)
realinstall: _kmodinstall
.ORDER: beforeinstall _kmodinstall
_kmodinstall:
.if defined(INSTALLSTRIPPEDMODULES)
	${INSTALL} -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG} ${DESTDIR}${KMODDIR}
	${OBJCOPY} --strip-debug ${DESTDIR}${KMODDIR}/${PROG}
.else
	${INSTALL} -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG} ${DESTDIR}${KMODDIR}
.endif
.endif # !target(realinstall)

.include <bsd.links.mk>

.endif # !target(install)

.if !target(load)
load:	${PROG}
	${KMODLOAD} -v ./${KMOD}.ko
.endif

.if !target(unload)
unload:
	${KMODUNLOAD} -v ${KMOD}
.endif

.for _src in ${SRCS:Mopt_*.h} ${SRCS:Muse_*.h}
CLEANFILES+=	${_src}
.if !target(${_src})
.if defined(BUILDING_WITH_KERNEL) && exists(${BUILDING_WITH_KERNEL}/${_src})
${_src}: ${BUILDING_WITH_KERNEL}/${_src}
# we do not have to copy these files any more, the kernel build
# directory is included in the path now.
#	cp ${BUILDING_WITH_KERNEL}/${_src} ${.TARGET}
.else
${_src}:
	touch ${.TARGET}
.endif	# BUILDING_WITH_KERNEL
.endif
.endfor

MFILES?= kern/bus_if.m kern/device_if.m bus/iicbus/iicbb_if.m \
    bus/iicbus/iicbus_if.m bus/isa/isa_if.m dev/netif/mii_layer/miibus_if.m \
    bus/pccard/card_if.m bus/pccard/power_if.m bus/pci/pci_if.m \
    bus/pci/pcib_if.m \
    bus/ppbus/ppbus_if.m bus/smbus/smbus_if.m \
    dev/acpica/acpi_if.m dev/acpica/acpi_wmi_if.m dev/disk/nata/ata_if.m \
    dev/sound/pcm/ac97_if.m dev/sound/pcm/channel_if.m \
    dev/sound/pcm/feeder_if.m dev/sound/pcm/mixer_if.m \
    libiconv/iconv_converter_if.m dev/agp/agp_if.m opencrypto/cryptodev_if.m \
    bus/mmc/mmcbus_if.m bus/mmc/mmcbr_if.m \
    dev/virtual/virtio/virtio/virtio_bus_if.m \
    dev/virtual/virtio/virtio/virtio_if.m

.if !defined(WANT_OLDUSB)
MFILES+=bus/u4b/usb_if.m
.else
MFILES+=bus/usb/usb_if.m
.endif

.for _srcsrc in ${MFILES}
.for _ext in c h
.for _src in ${SRCS:M${_srcsrc:T:R}.${_ext}}
CLEANFILES+=	${_src}
.if !target(${_src})
${_src}: @
.if exists(@)
${_src}: @/tools/makeobjops.awk @/${_srcsrc}
.endif

.if defined(BUILDING_WITH_KERNEL) && \
    exists(${BUILDING_WITH_KERNEL}/${_src})
.else
	awk -f @/tools/makeobjops.awk -- -${_ext} @/${_srcsrc}
.endif
.endif
.endfor # _src
.endfor # _ext
.endfor # _srcsrc

#.for _ext in c h
#.if ${SRCS:Mvnode_if.${_ext}} != ""
#CLEANFILES+=	vnode_if.${_ext}
#vnode_if.${_ext}: @
#.if exists(@)
#vnode_if.${_ext}: @/tools/vnode_if.awk @/kern/vnode_if.src
#.endif
#	awk -f @/tools/vnode_if.awk -- -${_ext} @/kern/vnode_if.src
#.endif
#.endfor

.if !empty(SRCS:Mmiidevs.h)
CLEANFILES+=	miidevs.h
.if !exists(@)
miidevs.h: @
.else
miidevs.h: @/tools/miidevs2h.awk @/dev/netif/mii_layer/miidevs
.endif
	${AWK} -f @/tools/miidevs2h.awk @/dev/netif/mii_layer/miidevs
.endif

.if !empty(SRCS:Mpccarddevs.h)
CLEANFILES+=	pccarddevs.h
.if !exists(@)
pccarddevs.h: @
.else
pccarddevs.h: @/tools/pccarddevs2h.awk @/bus/pccard/pccarddevs
.endif
	${AWK} -f @/tools/pccarddevs2h.awk @/bus/pccard/pccarddevs
.endif

.if !empty(SRCS:Mpcidevs.h)
CLEANFILES+=	pcidevs.h
.if !exists(@)
pcidevs.h: @
.else
pcidevs.h: @/tools/pcidevs2h.awk @/bus/pci/pcidevs
.endif
	${AWK} -f @/tools/pcidevs2h.awk @/bus/pci/pcidevs
.endif

.if !empty(SRCS:Musbdevs.h)
CLEANFILES+=	usbdevs.h
.if !exists(@)
usbdevs.h: @
.else
usbdevs.h: @/tools/usbdevs2h.awk @/bus/u4b/usbdevs
.endif
	${AWK} -f @/tools/usbdevs2h.awk @/bus/u4b/usbdevs -h
.endif

.if !empty(SRCS:Musbdevs_data.h)
CLEANFILES+=	usbdevs_data.h
.if !exists(@)
usbdevs_data.h: @
.else
usbdevs_data.h: @/tools/usbdevs2h.awk @/bus/u4b/usbdevs
.endif
	${AWK} -f @/tools/usbdevs2h.awk @/bus/u4b/usbdevs -d
.endif

.if !empty(SRCS:Macpi_quirks.h)
CLEANFILES+=	acpi_quirks.h
.if !exists(@)
acpi_quirks.h: @
.else
acpi_quirks.h: @/tools/acpi_quirks2h.awk @/dev/acpica/acpi_quirks
.endif
	${AWK} -f @/tools/acpi_quirks2h.awk @/dev/acpica/acpi_quirks
.endif

.if !empty(SRCS:Massym.s)
CLEANFILES+=	assym.s genassym.o
assym.s: genassym.o
.if defined(BUILDING_WITH_KERNEL)
genassym.o: opt_global.h
.endif
.if !exists(@)
assym.s: @
.else
assym.s: @/kern/genassym.sh
.endif
	sh @/kern/genassym.sh genassym.o > ${.TARGET}
.if exists(@)
genassym.o: @/platform/${MACHINE_PLATFORM}/${MACHINE_ARCH}/genassym.c          
.endif
genassym.o: @ ${SRCS:Mopt_*.h}
	${CC} -c ${CFLAGS:N-fno-common:N-mcmodel=small} ${WERROR} \
	@/platform/${MACHINE_PLATFORM}/${MACHINE_ARCH}/genassym.c
.endif

regress:

.include <bsd.dep.mk>

.if !exists(${DEPENDFILE})
${OBJS}: ${SRCS:M*.h}
.endif

.include <bsd.obj.mk>
.include "bsd.kern.mk"

# Behaves like MODULE_OVERRIDE
.if defined(KLD_DEPS)
all: _kdeps_all
_kdeps_all: @
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make all
.endfor
depend: _kdeps_depend
_kdeps_depend: @
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make depend
.endfor
install: _kdeps_install
_kdeps_install: @
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make install
.endfor
clean: _kdeps_clean
_kdeps_clean: @
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make clean
.endfor
.endif
