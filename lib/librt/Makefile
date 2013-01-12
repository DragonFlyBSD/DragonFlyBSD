LIB=rt
SHLIB_MAJOR=0
WARNS?=2
LDADD+=-pthread
DPADD+=${LIBPTHREAD}

SRCS+= mq.c aio.c

MAN+=	mq_close.2 mq_getattr.2 mq_notify.2 mq_open.2 mq_receive.2 \
	mq_send.2 mq_setattr.2 mq_unlink.2

MAN+=	aio_cancel.3 aio_error.3 aio_read.3 aio_return.3 \
	aio_suspend.3 aio_write.3

MLINKS+=mq_receive.2 mq_timedreceive.2 \
	mq_send.2 mq_timedsend.2

.include <bsd.lib.mk>
