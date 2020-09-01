.PATH:	${.CURDIR}/.. ${.CURDIR}/../ttm
KMOD	= drm
SRCS	= \
	ati_pcigart.c \
	drm_atomic.c \
	drm_atomic_helper.c \
	drm_auth.c \
	drm_blend.c \
	drm_bridge.c \
	drm_bufs.c \
	drm_cache.c \
	drm_color_mgmt.c \
	drm_connector.c \
	drm_context.c \
	drm_crtc.c \
	drm_crtc_helper.c \
	drm_dma.c \
	drm_dp_aux_dev.c \
	drm_dp_dual_mode_helper.c \
	drm_dp_helper.c \
	drm_dp_mst_topology.c \
	drm_dragonfly.c \
	drm_drv.c \
	drm_dumb_buffers.c \
	drm_edid.c \
	drm_edid_load.c \
	drm_encoder.c \
	drm_encoder_slave.c \
	drm_fb_helper.c \
	drm_file.c \
	drm_fourcc.c \
	drm_framebuffer.c \
	drm_gem.c \
	drm_global.c \
	drm_hashtab.c \
	drm_ioctl.c \
	drm_irq.c \
	drm_lock.c \
	drm_memory.c \
	drm_mipi_dsi.c \
	drm_mm.c \
	drm_mode_config.c \
	drm_mode_object.c \
	drm_modes.c \
	drm_modeset_lock.c \
	drm_modeset_helper.c \
	drm_panel.c \
	drm_pci.c \
	drm_plane.c \
	drm_plane_helper.c \
	drm_prime.c \
	drm_print.c \
	drm_probe_helper.c \
	drm_property.c \
	drm_rect.c \
	drm_scatter.c \
	drm_scdc_helper.c \
	drm_sysctl.c \
	drm_sysfs.c \
	drm_vm.c \
	drm_vma_manager.c \
	linux_async.c \
	linux_compat.c \
	linux_completion.c \
	linux_dma.c \
	linux_dma-buf.c \
	linux_dmi.c \
	linux_fence.c \
	linux_hdmi.c \
	linux_hrtimer.c \
	linux_i2c.c \
	linux_interval_tree.c \
	linux_iomapping.c \
	linux_irq.c \
	linux_kobject.c \
	linux_kthread.c \
	linux_list_sort.c \
	linux_printf.c \
	linux_radix.c \
	linux_rcu.c \
	linux_reservation.c \
	linux_scatterlist.c \
	linux_sched.c \
	linux_shmem.c \
	linux_sort.c \
	linux_tasklet.c \
	linux_vmalloc.c \
	linux_wait.c \
	linux_workqueue.c \
	linux_wwmutex.c \
	ttm_tt.c \
	ttm_bo_util.c \
	ttm_bo.c \
	ttm_bo_manager.c \
	ttm_execbuf_util.c \
	ttm_memory.c \
	ttm_page_alloc.c \
	ttm_bo_vm.c \
	ttm_module.c

# if CONFIG_AGP is set
#SRCS+=	drm_agpsupport.c \

SRCS	+= \
	opt_ddb.h \
	opt_drm.h \
	opt_vm.h \
	bus_if.h \
	device_if.h \
	pci_if.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/drm
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi
KCFLAGS+= -include ${SYSDIR}/dev/drm/kconfig.h
KCFLAGS+= -DKBUILD_MODNAME=\"$(KMOD)\"

.include <bsd.kmod.mk>
