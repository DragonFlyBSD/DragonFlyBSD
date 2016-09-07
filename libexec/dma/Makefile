SUBDIR=	dma-mbox-create default-conf mailer-conf

.if !defined(NO_LIBRESSL)
SUBDIR+= dma
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
