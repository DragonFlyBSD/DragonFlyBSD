# if wrapper mode
.if defined(NO_TOOLCHAIN)
.include "Makefile.zhack"
.else

# normal mode
SUBDIR= 	libbfd libiberty libgnu libopcodes libdecnumber libreadline
SUBDIR+=	libgdb gdb kgdb gdbtui

.include <bsd.subdir.mk>
.endif
