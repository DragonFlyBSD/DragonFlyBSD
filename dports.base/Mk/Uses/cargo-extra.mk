# $FreeBSD$
#
# This file contains logic to ease patching cargo crates.
#
# Feature:	cargo
# Usage:	USES=cargo
# Valid ARGS:	cargo:extra
#
# MAINTAINER: rust@FreeBSD.org

.if !defined(_INCLUDE_USES_CARGO_EXTRA_MK)
_INCLUDE_USES_CARGO_EXTRA_MK=	yes

# XXX is there really no better place?
.if ${OPSYS} == DragonFly
CARGO_EXTRA_PATCHDIR=	${PORTSDIR}/lang/rust/dragonfly
.else
CARGO_EXTRA_PATCHDIR=	${PORTSDIR}/lang/rust/files
.endif

# Iterate over CARGO_CRATES and add to EXTRA_PATCHES patches to specific versions
# of that specific crate
_LOCAL_CARGO_CRATES=${CARGO_CRATES:N*@git+*}
.for _crate in ${_LOCAL_CARGO_CRATES}
.if exists(${CARGO_EXTRA_PATCHDIR}/extra-${_crate})
EXTRA_PATCHES+= ${CARGO_EXTRA_PATCHDIR}/extra-${_crate}
.endif
.endfor

.endif
