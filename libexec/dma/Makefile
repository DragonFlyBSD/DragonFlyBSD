SUBDIR=	dma-mbox-create default-conf mailer-conf

.if !defined(NO_CRYPT)
SUBDIR+= dma
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
