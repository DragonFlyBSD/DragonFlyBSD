KMOD=	nvgpu
SRCS=	nvgpu_device.c nvgpu_debug.c nvgpu_chip.c nvgpu_proc.c
SRCS+=	nvgpu_syscall.c nvgpu_info.c nvgpu_nvif.c nvgpu_vm.c nvgpu_bo.c nvgpu_ttm.c
SRCS+=	nvgpu_channel.c
SRCS+=	nvgpu_unload.c nvgpu_intr.c nvgpu_fence.c nvgpu_future.c
SRCS+=	nvgpu_sched.c nvdrm_sync.c nvdrm_prime.c
SRCS+=	nvgpu_display.c nvgpu_display_method.c
SRCS+=	nvdrm_drv.c nvdrm_file.c nvdrm_ioctl.c nvdrm_kms.c
SRCS+=	nvgsp_state.c nvgsp_boot.c nvgsp_event.c nvgsp_rpc.c
SRCS+=	nvgsp_seq.c nvgsp_dma.c nvgsp_bios.c nvgsp_fw.c nvgsp_falcon.c
SRCS+=	nvgsp_fwsec.c nvgsp_meta.c nvgsp_boot_image.c
SRCS+=	nvgsp_libos.c nvgsp_booter.c nvgsp_static.c
SRCS+=	nvgsp_vram.c nvgsp_bar.c nvgsp_vmm.c nvgsp_rm.c
SRCS+=	nvgsp_channel.c nvgsp_disp.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_ddb.h opt_acpi.h opt_drm.h
SRCS+=	opt_ktr.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/drm
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi/drm
KCFLAGS+= -DKTR_NVGPU
CWARNFLAGS+= -Wno-error=cast-qual

.include <bsd.kmod.mk>
