#
# DragonFly source tree path variables.
#
# DF.SRCTOP	absolute path to the top of the source tree
#		found by walking up from .CURDIR
# DF.RELDIR	this directory relative to DF.SRCTOP
#		(e.g., "gnu/usr.bin/cc120")
# DF.RELTOP	relative path from here back to DF.SRCTOP
#		(e.g., "../../..")
# DF.OBJTOP	absolute path to the top of the object tree
#

.if !target(__<bsd.dragonfly.mk>__)
__<bsd.dragonfly.mk>__:

_DF_SRCTOP:=
_DF_DIR:=	${.CURDIR}
.for _DF_LEVEL in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
.  if empty(_DF_SRCTOP) && exists(${_DF_DIR}/Makefile.inc1) && \
      exists(${_DF_DIR}/share/mk/sys.mk)
_DF_SRCTOP:=	${_DF_DIR}
.  endif
_DF_DIR:=	${_DF_DIR:H}
.endfor

.if empty(_DF_SRCTOP)
.error Cannot find the DragonFly source tree.
.endif

DF.SRCTOP:=	${_DF_SRCTOP}

.if ${.CURDIR} == ${DF.SRCTOP}
DF.RELDIR=	.
DF.RELTOP=	.
.else
DF.RELDIR:=	${.CURDIR:S,${DF.SRCTOP}/,,}
DF.RELTOP:=	${DF.RELDIR:C,[^/]+,..,g}
.endif

DF.OBJTOP:=	${.OBJDIR}/${DF.RELTOP}
DF.OBJTOP:=	${DF.OBJTOP:tA}

.undef _DF_SRCTOP
.undef _DF_DIR
.undef _DF_LEVEL

.endif # !target(__<bsd.dragonfly.mk>__)
