/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
 *	Zhichao Li <Zhichao1.Li@Dell.com>
 *	Pilar Liang <Pillar.Liang@Dellteam.com>
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/conf.h>

#include <bus/u4b/usb.h>
#define USB_DEBUG_VAR uvc_debug
#include <bus/u4b/usb_debug.h>
#include <bus/u4b/usbdi.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_v4l2.h"

int v4l2_not_allowed;

static SYSCTL_NODE(_hw_usb, OID_AUTO, v4l2, CTLFLAG_RW, 0, "USB v4l2");

TUNABLE_INT("hw.usb.v4l2.v4l2_not_allowed", &v4l2_not_allowed);
SYSCTL_INT(_hw_usb_v4l2, OID_AUTO, v4l2_not_allowed, CTLFLAG_RW,
		    &v4l2_not_allowed, 0, "V4L2 Permission");

static int
uvc_v4l2_acquire_pri(struct uvc_v4l2_cdev_priv *priv)
{
	DPRINTF("=======v4l2 acquire pri %s-%s %d-%d mem:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		priv->num);
	DPRINTF("pri:%lu video pri:%lu\n", priv->work_pri, priv->v->pri);

	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE) {
		DPRINTF("%s already get\n", __func__);
		return 0;
	}

	if (atomic_cmpset_64(&priv->v->pri, 0, 1) == 0) {
		DPRINTF("%s busy\n", __func__);
		return EBUSY;
	}

	priv->work_pri = UVC_V4L2_PRI_ACTIVE;

	return 0;
}

static void
uvc_v4l2_dismiss_pri(struct uvc_v4l2_cdev_priv *priv)
{
	DPRINTF("%s num:%lu\n", __func__, priv->num);
	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE) {
		DPRINTF("%s free\n", __func__);
		atomic_store_64(&priv->v->pri, 0);
	}

	priv->work_pri = UVC_V4L2_PRI_PASSIVE;
}

static int
uvc_v4l2_has_pri(struct uvc_v4l2_cdev_priv *priv)
{
	return priv->work_pri == UVC_V4L2_PRI_ACTIVE;
}

static int
uvc_v4l2_query_cap(struct uvc_drv_video *v, void *addr)
{
	struct v4l2_capability cap;
	struct usb_device *udev;
	char bus_info[128];

	memset(&cap, 0x00, sizeof(cap));
	strlcpy(cap.driver, UVC_DRIVER_NAME, sizeof(cap.driver));
	ksnprintf(cap.card, sizeof(cap.card), "%s", v->sc->name);

	if (!v->sc)
		return EINVAL;
	udev = v->sc->udev;
	if (!udev)
		return EINVAL;
	usbd_get_phys(udev, bus_info, sizeof(bus_info));
	ksnprintf(cap.bus_info, sizeof(cap.bus_info), "%s", bus_info);
	cap.version = V4L_VERSION(3, 14, 1);

	/*
	 *
	 * V4L2_CAP_VIDEO_CAPTURE input device
	 * V4L2_CAP_READWRITE read method
	 * V4L2_CAP_STREAMING mmap/userptr
	 *
	 */
	cap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_ASYNCIO | 0x80000000;
	cap.reserved[0] = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	memcpy(addr, &cap, sizeof(cap));
	return 0;
}

static int
uvc_v4l2_g_selection(struct uvc_drv_video *v, void *addr)
{
	struct v4l2_selection *p = addr;
	uint32_t old_type = p->type;
	int ret = 0;

	if (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = uvc_drv_get_selection(v, p);
	p->type = old_type;
	return ret;
}

__unused static int
uvc_v4l2_enumstd(struct v4l2_standard *std)
{
	std->id = V4L2_STD_UNKNOWN;

	std->framelines = (std->id & V4L2_STD_525_60) ? 525 : 625;
	if (std->id & V4L2_STD_525_60) {
		std->frameperiod.numerator = 1001;
		std->frameperiod.denominator = 30000;
	} else {
		std->frameperiod.numerator = 1;
		std->frameperiod.denominator = 25;
	}
	strncpy(std->name, "UVC Camera", sizeof(std->name) - 1);

	return 0;
}

static int
uvc_v4l2_cropcap(struct uvc_drv_video *v, void *addr)
{
	struct v4l2_cropcap *p = NULL;
	struct v4l2_selection s = {};
	uint32_t ret = 0;

	p = (struct v4l2_cropcap *)addr;

	p->pixelaspect.numerator = 1;
	p->pixelaspect.denominator = 1;

	s.type = p->type;

	if (s.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (s.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	ret = uvc_drv_get_pixelaspect();
	//515 is ENOIOCTLCMD in linux
	if (ret && ret != ENOTTY && ret != 515)
		return ret;
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE_BOUNDS;
	else
		s.target = V4L2_SEL_TGT_CROP_BOUNDS;

	ret = uvc_v4l2_g_selection(v, &s);
	if (ret)
		return ret;
	p->bounds = s.r;
	//obtaining defrect
	if (s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS)
		s.target = V4L2_SEL_TGT_COMPOSE_DEFAULT;
	else
		s.target = V4L2_SEL_TGT_CROP_DEFAULT;
	ret = uvc_v4l2_g_selection(v, &s);
	if (ret)
		return ret;

	p->defrect = s.r;

	return 0;
}

void
uvc_simple_frac(uint32_t *numerator, uint32_t *denominator,
	uint32_t max_iters, uint32_t threshold)
{
	uint32_t *coeff;
	uint32_t best_den, best_num, remainder;
	uint32_t i, j;

	 if (!numerator || !denominator ||
	     *denominator == 0 || max_iters == 0) {
		 return;
	 }

	coeff = (uint32_t *)kmalloc(max_iters * sizeof(uint32_t),
		M_UVC, M_ZERO | M_WAITOK);
	if (coeff == NULL)
		return;

	best_den = *numerator;
	best_num = *denominator;

	for (j = 0; j < max_iters && best_num != 0; ++j) {
		coeff[j] = best_den / best_num;
		if (coeff[j] >= threshold) {
			if (j < 2)
				j++;
			break;
		}

		remainder = best_den % best_num;
		best_den = best_num;
		best_num = remainder;
	}

	best_den = 0;
	best_num = 1;

	for (i = j; i > 0; --i) {
		remainder = best_num;
		best_num = coeff[i-1] * best_num + best_den;
		best_den = remainder;
	}

	*numerator = best_num;
	*denominator = best_den;
	kfree(coeff, M_UVC);
}

static int
uvc_v4l2_get_parm(struct uvc_drv_video *video, struct v4l2_streamparm *arg)
{
	uint32_t numerator, denominator;

	if (arg->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return EINVAL;

	numerator = UGETW(video->req.dwFrameInterval);
	denominator = 10000000;
	uvc_simple_frac(&numerator, &denominator, 8, 333);

	memset(arg, 0, sizeof(*arg));
	arg->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	arg->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	arg->parm.capture.capturemode = 0;
	arg->parm.capture.timeperframe.numerator = numerator;
	arg->parm.capture.timeperframe.denominator = denominator;
	arg->parm.capture.extendedmode = 0;
	arg->parm.capture.readbuffers = 0;

	return 0;
}

//only stub function
static int
uvc_v4l2_enum_input(struct uvc_drv_ctrl *ctrl, struct v4l2_input *input)
{
	uint32_t index = input->index;
	struct uvc_topo_node *it = NULL; // input terminal

	/*WARNING: to_be_implement here, hard code no SELECTOR */
	if (index)
		return EINVAL;

	it = STAILQ_FIRST(&ctrl->topo_nodes);
	if (!it)
		return EINVAL;

	memset(input, 0, sizeof(*input));
	input->index = index;
	strlcpy(input->name, it->node_name, sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static void
uvc_v4l2_dtor(void *data)
{
	struct uvc_v4l2_cdev_priv *priv = data;
	struct uvc_drv_video *v;

	v = priv->v;
	if (priv->work_pri == UVC_V4L2_PRI_ACTIVE) {
		atomic_store_64(&v->pri, 0);
	}

	atomic_subtract_64(&v->users, 1);

	kfree(data, M_UVC);

	DPRINTF("%s\n", __func__);
}

static int
uvc_v4l2_queryctrl(struct uvc_drv_video *v, struct v4l2_queryctrl *qc)
{
	int ret = EINVAL;

	if (qc == NULL)
		return ret;

	ret = uvc_query_v4l2_ctrl(v, qc);
	return ret;
}

static int
uvc_v4l2_querymenu(struct uvc_drv_video *v, struct v4l2_querymenu *qm)
{
	int ret = EINVAL;

	if (qm == NULL)
		return ret;

	ret = uvc_query_v4l2_menu(v, qm);
	return ret;
}

static int
uvc_v4l2_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = dev->si_drv1;

	if (v4l2_not_allowed != 0) {
		return ENXIO;			/* failure */
	}

	atomic_add_64(&v->users, 1);

	DPRINTF("===v4l2 open %s-%s %d-%d mem:%lu pri:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		v->users, v->pri);

	DPRINTF("====%s====%d %d %s\n", __func__, curthread->td_proc->p_pid,
		curthread->td_proc->p_pptr->p_pid,
		curthread->td_proc->p_pptr->p_comm);
	DPRINTF("enable:%lu\n", v->enable);

	priv = (struct uvc_v4l2_cdev_priv *)
		kmalloc(sizeof(*priv), M_UVC, M_ZERO | M_WAITOK);
	if (!priv) {
		DPRINTF("%s %d------>Error.\n", __func__, __LINE__);
		atomic_subtract_64(&v->users, 1);
		return ENOMEM;
	}

	priv->work_mode = UVC_V4L2_MODE_READ;
	priv->work_pri = UVC_V4L2_PRI_PASSIVE;
	priv->v = v;
	priv->num = v->users;
	(void)devfs_set_cdevpriv(ap->a_fpp ? *ap->a_fpp : NULL, priv, uvc_v4l2_dtor);
	return 0;
}

static int
uvc_v4l2_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uvc_drv_video *v = dev->si_drv1;
	struct uvc_v4l2_cdev_priv *priv;
	int ret;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&priv);
	if (ret != 0) {
		DPRINTF("error===================================\n");
		return (ret);
	}

	DPRINTF("===v4l2 begin to close %s-%s %d-%d num:%lu mem:%lu===\n",
		curthread->td_proc->p_comm, curthread->td_proc->p_pptr->p_comm,
		curthread->td_proc->p_pid, curthread->td_proc->p_pptr->p_pid,
		priv->num, v->users);
	DPRINTF("=vedio device pri:%lu--thispri:%lu\n", v->pri, priv->work_pri);
	DPRINTF("----%s----\n", __func__);

	if (uvc_v4l2_has_pri(priv)) {
		ret = uvc_drv_stop_video(v, 1);
		if (ret)
			DPRINTF("close stop video fault\n");
		uvc_buf_queue_free_bufs(&v->bq);
		uvc_v4l2_dismiss_pri(priv);
	}
	//atomic_subtract_64(&v->users, 1);

	return 0;
}

static int
uvc_v4l2_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	caddr_t data = ap->a_data;
	int fflag = ap->a_fflag;
	struct uvc_v4l2_cdev_priv *priv;
	struct uvc_drv_video *v = dev->si_drv1;

	struct v4l2_fmtdesc *f_d;
	struct v4l2_format *fmt;
	struct v4l2_streamparm *strp;

	struct uvc_data_format *rfmt;
	struct uvc_data_frame *rfrm;
	struct uvc_data_request req;
	struct v4l2_requestbuffers *rb;
	struct v4l2_buffer *buf;
	struct uvc_xu_control_query qry;
	int ret = ENOTTY;

	ret = devfs_get_cdevpriv(ap->a_fp, (void **)&priv);
	if (ret != 0) {
		DPRINTF("%s %d---------->err:%d\n", __func__, __LINE__, ret);
		return (ret);
	}

	/* check pri */
	switch (cmd) {
	/* case VIDIOC_S_INPUT:*/
	case VIDIOC_REQBUFS:
	case VIDIOC_S_PARM:
	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT:
		ret = uvc_v4l2_acquire_pri(priv);
		if (ret)
			return ret;
		break;

	case VIDIOC_QUERYBUF:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
		if (!uvc_v4l2_has_pri(priv))
			return EBUSY;
		break;
	default:
		break;
	}

	switch (cmd) {
	case VIDIOC_QUERYCAP:
		DPRINTF("VIDIOC_QUERYCAP\n");
		ret = uvc_v4l2_query_cap(v, data);
		break;

	case VIDIOC_G_PARM:
		DPRINTF("VIDIOC_G_PARM\n");
		ret = uvc_v4l2_get_parm(v, (struct v4l2_streamparm *)data);
		break;

	case VIDIOC_S_PARM:
		DPRINTF("VIDIOC_S_PARM\n");
		strp = (struct v4l2_streamparm *)data;
		if (strp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_drv_set_streampar(v, strp);
		break;

	case VIDIOC_ENUMINPUT:
		DPRINTF("VIDIOC_ENUMINPUT\n");
		ret = uvc_v4l2_enum_input(v->ctrl, (struct v4l2_input *)data);
		break;

	case VIDIOC_G_INPUT:
		kprintf("unsupport ioctl VIDIOC_G_INPUT.\n");
		*(int *)data = 0;
		break;
	case VIDIOC_ENUMSTD:
		kprintf("unsupport ioctl VIDIOC_ENUMSTD\n");
		ret = ENOTTY;
		break;

	case VIDIOC_QUERYCTRL:
		DPRINTF("VIDIOC_QUERYCTRL\n");
		ret = uvc_v4l2_queryctrl(v, (struct v4l2_queryctrl *)data);
		break;

	case VIDIOC_QUERYMENU:
		DPRINTF("VIDIOC_QUERYMENU.\n");
		ret = uvc_v4l2_querymenu(v, (struct v4l2_querymenu *)data);
		break;

	case VIDIOC_G_CTRL:
		kprintf("unsupport ioctl VIDIOC_G_CTRL.\n");
		ret = EINVAL;
		break;
	case VIDIOC_G_STD:
		kprintf("unsupport ioctl VIDIOC_G_STD.\n");
		ret = ENOTTY;
		break;

	case VIDIOC_CROPCAP:
		ret = uvc_v4l2_cropcap(v, data);
		break;

	case VIDIOC_ENUM_FMT:
		DPRINTF("VIDIOC_ENUM_FMT\n");
		f_d = (struct v4l2_fmtdesc *)data;
		if (f_d->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_drv_enum_v4l2_fmt(v, f_d);
		break;

	case VIDIOC_ENUM_FRAMESIZES:
		//DPRINTF("VIDIOC_ENUM_FRAMESIZES\n");
		ret = uvc_drv_enum_v4l2_framesizes(v,
			(struct v4l2_frmsizeenum *)data);
		break;

	case VIDIOC_ENUM_FRAMEINTERVALS:
		//DPRINTF("VIDIOC_ENUM_FRAMEINTERVAL\n");
		ret = uvc_drv_enum_v4l2_frameintervals(v,
			(struct v4l2_frmivalenum *)data);
		break;

	case VIDIOC_G_FMT:
		DPRINTF("VIDIOC_G_FMT\n");
		fmt = (struct v4l2_format *)data;
		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_drv_get_v4l2_fmt(v, fmt);
		break;

	case VIDIOC_TRY_FMT:
		DPRINTF("VIDIOC_TRY_FMT\n");
		fmt = (struct v4l2_format *)data;
		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_drv_try_v4l2_fmt(v, fmt, &req, NULL, NULL);
		break;

	case VIDIOC_S_FMT:
		DPRINTF("VIDIOC_S_FMT\n");
		fmt = (struct v4l2_format *)data;
		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_drv_try_v4l2_fmt(v, fmt, &req, &rfmt, &rfrm);
		if (!ret) {
			ret = uvc_drv_set_video(v, &req, rfmt, rfrm);
		}

		break;

	case VIDIOC_STREAMON:
		DPRINTF("VIDIOC_STREAMON\n");
		if (*(int *)data != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;

		ret = uvc_drv_start_video(v);

		break;

	case VIDIOC_STREAMOFF:
		DPRINTF("VIDIOC_STREAMOFF\n");
		if (*(int *)data != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;

		ret = uvc_drv_stop_video(v, 0);
		break;

	case VIDIOC_REQBUFS:
		DPRINTF("VIDIOC_REQBUFS default size:%u\n",
			UGETDW(v->req.dwMaxFrameSize));
		rb = (struct v4l2_requestbuffers *)data;
		if (rb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
			/* todo */rb->memory != V4L2_MEMORY_MMAP)
			return EINVAL;
		ret = uvc_buf_queue_req_bufs(&v->bq, &rb->count,
			UGETDW(v->req.dwMaxFrameSize));
		if (!ret) {
			if (!(v->cur_fmt->flags & UVC_FMT_FLAG_COMPRESSED)) {
				DPRINTF("drop incomplete\n");
				uvc_buf_queue_set_drop_flag(&v->bq);
			}
			priv->work_mode = (rb->count)
				? UVC_V4L2_MODE_MMAP : UVC_V4L2_MODE_READ;
		}
		break;

	case VIDIOC_EXPBUF:
		kprintf("unsupport ioctl VIDIOC_EXPBUF.\n");
		ret = ENOTTY;
		break;

	case VIDIOC_QUERYBUF:
		DPRINTF("VIDIOC_QUERYBUF\n");
		buf = (struct v4l2_buffer *)data;
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return EINVAL;
		ret = uvc_buf_queue_query_buf(&v->bq, buf);
		break;

	case VIDIOC_QBUF:
		//DPRINTF("VIDIOC_QBUF\n");
		buf = (struct v4l2_buffer *)data;
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
			buf->memory != V4L2_MEMORY_MMAP) {
			DPRINTF("memory:%d\n", buf->memory);
			return EINVAL;
		}
		ret = uvc_buf_queue_queue_buf(&v->bq, buf);
		if (ret)
			DPRINTF("return:%d\n", ret);
		return ret;

	case VIDIOC_DQBUF:
		//DPRINTF("VIDIOC_DQBUF\n");
		buf = (struct v4l2_buffer *)data;
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
			buf->memory != V4L2_MEMORY_MMAP) {
			return EINVAL;
		}

		ret = uvc_buf_queue_dequeue_buf(&v->bq, buf,
				(fflag & O_NONBLOCK)?1:0);
		if (ret)
			DPRINTF("return:%d\n", ret);
		return ret;

	case UVCIOC_CTRL_MAP:
		kprintf("unsupport ioctl UVCIOC_CTRL_MAP.\n");
		ret = ENXIO;
		break;

	case UVCIOC_CTRL_QUERY:
		if (copyin((void *)data, &qry, sizeof(qry)))
			return EFAULT;

		if (qry.size > 64)
			return ENXIO;
		ret = uvc_drv_xu_ctrl_query(v, &qry);
		break;

	case VIDIOC_LOG_STATUS:
		ret = 0; /* UVC can't support VIDIOC_LOG_STATUS */
		break;

	default:
		kprintf("%lx-%s need to be implement\n", cmd, __func__);
		//ret = EINVAL;
		ret = ENOTTY;
		break;
	}

	return ret;
}

static int
uvc_v4l2_mmap(struct dev_mmap_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uvc_drv_video *v = dev->si_drv1;
	vm_offset_t offset = ap->a_offset;
	vm_paddr_t paddr = 0;
	int res;

	res = uvc_buf_queue_mmap(&v->bq, &paddr, offset);
	ap->a_result = paddr;

	return res;
}

static int
uvc_v4l2_read(struct dev_read_args *ap __unused)
{
	DPRINTF("%s\n", __func__);
	return EINVAL;
}

static int
uvc_v4l2_write(struct dev_write_args *ap __unused)
{
	DPRINTF("%s\n", __func__);
	return EINVAL;
}

static int
uvc_v4l2_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uvc_drv_video *v = dev->si_drv1;
	struct knote *kn = ap->a_kn;

	ap->a_result = uvc_buf_queue_kqfilter(&v->bq, kn);

	return (0);
}

static struct dev_ops uvc_v4l2_cdevsw = {
	{ .name = UVC_V4L2_DEVICE_NAME,
	  .maj = 0,
	  .flags = D_MPSAFE | D_MEM
	},
	.d_open	=	uvc_v4l2_open,
	.d_read	=	uvc_v4l2_read,
	.d_write =	uvc_v4l2_write,
	.d_close =	uvc_v4l2_close,
	.d_ioctl =	uvc_v4l2_ioctl,
	.d_mmap =	uvc_v4l2_mmap,
	.d_kqfilter =	uvc_v4l2_kqfilter,
};

void
uvc_v4l2_unreg(struct uvc_drv_video *v)
{
	DPRINTF("%s\n", __func__);
	/* destroy v4l2 */
	if (v->v4l2) {

		if (v->v4l2->cdev)
			destroy_dev(v->v4l2->cdev);

		kfree(v->v4l2, M_UVC);
	}
	/* remove */
	v->v4l2 = NULL;
}

int
uvc_v4l2_reg(struct uvc_drv_video *v)
{
	struct cdev *cdev;
	struct uvc_v4l2 *v4l2;
	int ret;

	DPRINTF("%s\n", __func__);
	v4l2 = kmalloc(sizeof(*v4l2), M_UVC, M_ZERO | M_WAITOK);
	if (!v4l2)
		return ENOMEM;

	cdev = make_dev(&uvc_v4l2_cdevsw, v->unit,
			UID_ROOT, GID_VIDEO, 0666,
			"%s%lu", UVC_V4L2_DEVICE_NAME, v->unit);
	if (cdev == NULL) {
		DPRINTF("failed to create v4l2 char device.\n");
		return ret;
	}
	cdev->si_drv1 = v;

	make_dev_alias(cdev, "char/%u:%lu",
		       LINUX_MAJAR, LINUX_MINOR + v->unit);

	v4l2->cdev = cdev;
	v->v4l2 = v4l2;

	return 0;
}
