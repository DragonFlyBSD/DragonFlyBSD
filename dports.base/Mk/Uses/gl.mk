# Use OpenGL and related libraries and ports
# 
# Feature:	gl
# Usage:	USES=gl
#		USE_GL=egl gbm gl glesv1 glesv2 glew glu glut opengl
#		
#		USE_GL specifies which GL components to add as dependencies.
#		Not specifying USE_GL with USES=gl is an error.
#		USE_GL=yes implies USE_GL=glu.  This is deprecated
# 
# MAINTAINER:	x11@FreeBSD.org

.if !defined(_INCLUDE_USES_GL_MK)
_INCLUDE_USES_GL_MK=yes

_GL_egl_LIB_DEPENDS=		libEGL.so:graphics/libglvnd
_GL_gbm_LIB_DEPENDS=		libgbm.so:graphics/${GL_DEFAULT}
_GL_gl_LIB_DEPENDS=		libGL.so:graphics/libglvnd
_GL_gl_USE_XORG=		xorgproto
_GL_glesv1_LIB_DEPENDS=		libGLESv1_CM.so:graphics/libglvnd
_GL_glesv2_LIB_DEPENDS=		libGLESv2.so:graphics/libglvnd
_GL_glew_LIB_DEPENDS=		libGLEW.so:graphics/glew
_GL_glu_LIB_DEPENDS=		libGLU.so:graphics/libGLU
_GL_glu_USE_XORG=		xorgproto
_GL_glut_LIB_DEPENDS=		libglut.so:graphics/freeglut
_GL_opengl_LIB_DEPENDS=		libOpenGL.so:graphics/libglvnd

.  if !empty(gl_ARGS)
IGNORE=	USES=gl takes no arguments
.  endif

.  if !defined(USE_GL)
IGNORE=		need to specify gl component with USE_GL
.  elif ${USE_GL:tl} == yes
DEV_WARNING+=	"USE_GL=yes is deprecated, please add USE_GL=glu (default) or specify component"
USE_GL=		glu
.  endif

.  for _component in ${USE_GL}
.    if !defined(_GL_${_component}_LIB_DEPENDS)
IGNORE=		uses unknown GL component
.    else
USE_XORG+=	${_GL_${_component}_USE_XORG}
LIB_DEPENDS+=	${_GL_${_component}_LIB_DEPENDS}
.    endif
.  endfor

# We only need to include xorg.mk if we want USE_XORG modules
.  if defined(USE_XORG) && !empty(USE_XORG)
.include "${USESDIR}/xorg.mk"
.  endif

# _INCLUDE_USES_GL_MK
.endif
