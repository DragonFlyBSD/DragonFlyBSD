/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * UVC spec:https://www.usb.org/sites/default/files/USB_Video_Class_1_5.zip
 */

#ifndef _DEV_USB_UVC_BUF_H
#define _DEV_USB_UVC_BUF_H

#include <sys/event.h>

#define	UVC_BUF_MAX_BUFFERS		8
#define UVC_BUF_QUEUE_IS_RUNNING(bq)	(bq->buf_count != 0)

enum uvc_buf_state {
	UVC_BUF_STATE_IDLE	=	0,
	UVC_BUF_STATE_QUEUED	=	1,
	UVC_BUF_STATE_ACTIVE	=	2,
	UVC_BUF_STATE_DONE	=	3,
	UVC_BUF_STATE_ERROR	=	4,
};

#define UVC_BUFFER_QUEUE_WORKING		(1 << 0)
#define UVC_BUFFER_QUEUE_DISCONNECTED		(1 << 1)
#define UVC_BUFFER_QUEUE_DROP_INCOMPLETE	(1 << 2)

struct uvc_buf {
	void		*mem;
	uint64_t	offset;
	uint64_t	status;
	uint64_t	index;
	struct v4l2_buffer vbuf;

	STAILQ_ENTRY(uvc_buf) link;
};

struct uvc_drv_video;
struct uvc_buf_queue {
	struct lock	mtx;
	struct cv	io_cv;
	struct uvc_drv_video *video;

	struct kqinfo	sel;

	void		*mem;
	uint64_t	status;
	uint64_t	flags;
	uint64_t	seq;

	uint64_t	buf_size;
	uint64_t	buf_count;
	struct uvc_buf	buf[UVC_BUF_MAX_BUFFERS];

	STAILQ_HEAD(, uvc_buf)	consumer;
	STAILQ_HEAD(, uvc_buf)	product;
};

extern void uvc_buf_queue_init(struct uvc_drv_video *v,
	struct uvc_buf_queue *queue);
extern int uvc_buf_queue_enable(struct uvc_buf_queue *queue);
extern int uvc_buf_queue_disable(struct uvc_buf_queue *queue);
extern int uvc_buf_queue_kqfilter(struct uvc_buf_queue *queue,
	struct knote *kn);
extern int uvc_buf_queue_req_bufs(struct uvc_buf_queue *queue,
	uint32_t *count, uint32_t size);
void uvc_buf_queue_free_bufs(struct uvc_buf_queue *queue);
void uvc_buf_queue_set_drop_flag(struct uvc_buf_queue *queue);
extern int uvc_buf_queue_query_buf(struct uvc_buf_queue *queue,
	struct v4l2_buffer *vbuf);
extern int uvc_buf_queue_mmap(struct uvc_buf_queue *queue,
	vm_paddr_t *paddr, vm_offset_t offset);
extern int uvc_buf_queue_queue_buf(struct uvc_buf_queue *queue,
	struct v4l2_buffer *vbuf);
extern int uvc_buf_queue_dequeue_buf(struct uvc_buf_queue *queue,
	struct v4l2_buffer *vbuf, int nonblock);
struct usb_page_cache;
extern int
uvc_buf_sell_buf(struct uvc_buf_queue *bq, struct usb_page_cache *pc,
	usb_frlength_t offset, usb_frlength_t len, uint32_t finish,
	uint8_t fid);
extern int
uvc_bulkbuf_sell_buf(struct uvc_buf_queue *bq,
		struct usb_page_cache *pc, usb_frlength_t offset,
		usb_frlength_t len,
		usb_frlength_t actlen,
		usb_frlength_t maxFramelen);
extern int uvc_buf_reset_buf(struct uvc_buf_queue *bq);

#endif /* end _DEV_USB_UVC_BUF_H */
