#
# Makefile for the drm/amdgpu driver.
#

KMOD=	amdgpu

.PATH:	${.CURDIR}/../../scheduler \
	${.CURDIR}/../powerplay \
	${.CURDIR}/../powerplay/hwmgr \
	${.CURDIR}/../powerplay/smumgr \
	${.CURDIR}/../lib \
	${.CURDIR}/../display/amdgpu_dm \
	${.CURDIR}/../display/dc \
	${.CURDIR}/../display/dc/basics \
	${.CURDIR}/../display/dc/bios \
	${.CURDIR}/../display/dc/bios/dce80 \
	${.CURDIR}/../display/dc/bios/dce110 \
	${.CURDIR}/../display/dc/bios/dce112 \
	${.CURDIR}/../display/dc/calcs \
	${.CURDIR}/../display/dc/core \
	${.CURDIR}/../display/dc/dce \
	${.CURDIR}/../display/dc/gpio \
	${.CURDIR}/../display/dc/gpio/dce110 \
	${.CURDIR}/../display/dc/gpio/dce120 \
	${.CURDIR}/../display/dc/gpio/dce80 \
	${.CURDIR}/../display/dc/gpio/dcn10 \
	${.CURDIR}/../display/dc/gpio/diagnostics \
	${.CURDIR}/../display/dc/i2caux \
	${.CURDIR}/../display/dc/i2caux/dce100 \
	${.CURDIR}/../display/dc/i2caux/dce110 \
	${.CURDIR}/../display/dc/i2caux/dce112 \
	${.CURDIR}/../display/dc/i2caux/dce120 \
	${.CURDIR}/../display/dc/i2caux/dce80 \
	${.CURDIR}/../display/dc/i2caux/dcn10 \
	${.CURDIR}/../display/dc/i2caux/diagnostics \
	${.CURDIR}/../display/dc/irq \
	${.CURDIR}/../display/dc/irq/dce110 \
	${.CURDIR}/../display/dc/irq/dce120 \
	${.CURDIR}/../display/dc/irq/dce80 \
	${.CURDIR}/../display/dc/irq/dcn10 \
	${.CURDIR}/../display/dc/virtual \
	${.CURDIR}/../display/dc/dcn10 \
	${.CURDIR}/../display/dc/dml \
	${.CURDIR}/../display/dc/dce120 \
	${.CURDIR}/../display/dc/dce112 \
	${.CURDIR}/../display/dc/dce110 \
	${.CURDIR}/../display/dc/dce100 \
	${.CURDIR}/../display/dc/dce80 \
	${.CURDIR}/../display/modules/freesync \
	${.CURDIR}/../display/modules/color \
	${.CURDIR}/../display/modules/info_packet

SRCS=	amdgpu_drv.c

SRCS+=	chash.c

# add KMS driver
SRCS+= amdgpu_device.c amdgpu_kms.c \
	amdgpu_atombios.c atombios_crtc.c amdgpu_connectors.c \
	atom.c amdgpu_fence.c amdgpu_ttm.c amdgpu_object.c amdgpu_gart.c \
	amdgpu_encoders.c amdgpu_display.c amdgpu_i2c.c \
	amdgpu_fb.c amdgpu_gem.c amdgpu_ring.c \
	amdgpu_cs.c amdgpu_bios.c amdgpu_benchmark.c amdgpu_test.c \
	amdgpu_pm.c atombios_dp.c amdgpu_afmt.c amdgpu_trace_points.c \
	atombios_encoders.c amdgpu_sa.c atombios_i2c.c \
	amdgpu_prime.c amdgpu_vm.c amdgpu_ib.c amdgpu_pll.c \
	amdgpu_ucode.c amdgpu_bo_list.c amdgpu_ctx.c amdgpu_sync.c \
	amdgpu_gtt_mgr.c amdgpu_vram_mgr.c amdgpu_virt.c amdgpu_atomfirmware.c \
	amdgpu_vf_error.c amdgpu_sched.c amdgpu_debugfs.c \
	amdgpu_ids.c \
	amdgpu_gmc.c amdgpu_xgmi.c

SRCS+= \
	vi.c mxgpu_vi.c nbio_v6_1.c soc15.c emu_soc.c mxgpu_ai.c nbio_v7_0.c vega10_reg_init.c \
	vega20_reg_init.c nbio_v7_4.o

# add DF block
SRCS+= \
	df_v1_7.c \
	df_v3_6.c

# add GMC block
SRCS+= \
	gmc_v7_0.c \
	gmc_v8_0.c \
	gfxhub_v1_0.c mmhub_v1_0.c gmc_v9_0.c gfxhub_v1_1.c

# add IH block
SRCS+= \
	amdgpu_irq.c \
	amdgpu_ih.c \
	iceland_ih.c \
	tonga_ih.c \
	cz_ih.c \
	vega10_ih.c

# add PSP block
SRCS+= \
	amdgpu_psp.c \
	psp_v3_1.c \
	psp_v10_0.c \
	psp_v11_0.c

# add SMC block
SRCS+= \
	amdgpu_dpm.c

# add DCE block
SRCS+= \
	dce_v10_0.c \
	dce_v11_0.c \
	dce_virtual.c

# add GFX block
SRCS+= \
	amdgpu_gfx.c \
	gfx_v8_0.c \
	gfx_v9_0.c

# add async DMA block
SRCS+= \
	amdgpu_sdma.c \
	sdma_v2_4.c \
	sdma_v3_0.c \
	sdma_v4_0.c

# add UVD block
SRCS+= \
	amdgpu_uvd.c \
	uvd_v5_0.c \
	uvd_v6_0.c \
	uvd_v7_0.c

# add VCE block
SRCS+= \
	amdgpu_vce.c \
	vce_v3_0.c \
	vce_v4_0.c

# add VCN block
SRCS+= \
	amdgpu_vcn.c \
	vcn_v1_0.c

# add amdkfd interfaces
SRCS+= amdgpu_amdkfd.c

# add cgs
SRCS+= amdgpu_cgs.c

# GPU scheduler
SRCS+= \
	sched_entity.c \
	sched_fence.c \
	sched_main.c \
	amdgpu_job.c

SRCS+= amdgpu_acpi.c

SRCS+= amd_powerplay.c

# powerplay/hwmgr
SRCS+= hwmgr.c processpptables.c \
		hardwaremanager.c smu8_hwmgr.c \
		pppcielanes.c\
		process_pptables_v1_0.c ppatomctrl.c ppatomfwctrl.c \
		smu7_hwmgr.c smu7_powertune.c smu7_thermal.c \
		smu7_clockpowergating.c \
		vega10_processpptables.c vega10_hwmgr.c vega10_powertune.c \
		vega10_thermal.c smu10_hwmgr.c pp_psm.c\
		vega12_processpptables.c vega12_hwmgr.c \
		vega12_thermal.c \
		pp_overdriver.c smu_helper.c \
		vega20_processpptables.c vega20_hwmgr.c vega20_powertune.c \
		vega20_thermal.c

# powerplay/smumgr
SRCS+= smumgr.c smu8_smumgr.c tonga_smumgr.c fiji_smumgr.c \
	  polaris10_smumgr.c iceland_smumgr.c \
	  smu7_smumgr.c vega10_smumgr.c smu10_smumgr.c ci_smumgr.c \
	  vega12_smumgr.c vegam_smumgr.c smu9_smumgr.c vega20_smumgr.c

# display core component (CONFIG_DRM_AMD_DC)
# amdgpu_dm
SRC+= amdgpu_dm.c amdgpu_dm_irq.c amdgpu_dm_mst_types.c amdgpu_dm_color.c
# ifneq ($(CONFIG_DRM_AMD_DC),)
SRC+= amdgpu_dm_services.c amdgpu_dm_helpers.c amdgpu_dm_pp_smu.c

# dc
SRCS+= amdgpu_dm.c amdgpu_dm_irq.c amdgpu_dm_mst_types.c amdgpu_dm_color.c
SRCS+= amdgpu_dm_services.c amdgpu_dm_helpers.c amdgpu_dm_pp_smu.c

# dc/basics
SRCS+= conversion.c fixpt31_32.c \
	log_helpers.c vector.c

# dc/bios
SRCS+= bios_parser.c bios_parser_interface.c  bios_parser_helper.c \
	command_table.c command_table_helper.c bios_parser_common.c
SRCS+= command_table2.c command_table_helper2.c bios_parser2.c
# DCE 8x
# All DCE8.x are derived from DCE8.0, so 8.0 MUST be defined if ANY of
# DCE8.x is compiled.
SRCS+= command_table_helper_dce80.c

# DCE 11x
SRCS+= command_table_helper_dce110.c
SRCS+= command_table_helper_dce112.c
SRCS+= command_table_helper2_dce112.c

# dc/calcs
SRCS+= dce_calcs.c bw_fixed.c custom_float.c

# ifdef CONFIG_DRM_AMD_DC_DCN1_0
SRCS+= dcn_calcs.c dcn_calc_math.c dcn_calc_auto.c

# dc/dce
SRCS+= dce_audio.c dce_stream_encoder.c dce_link_encoder.c dce_hwseq.c \
	dce_mem_input.c dce_clock_source.c dce_scl_filters.c dce_transform.c \
	dce_clocks.c dce_opp.c dce_dmcu.c dce_abm.c dce_ipp.c dce_aux.c \
	dce_i2c.c dce_i2c_hw.c dce_i2c_sw.c

# dc/gpio
SRCS+= gpio_base.c gpio_service.c hw_factory.c \
       hw_gpio.c hw_hpd.c hw_ddc.c hw_translate.c

# DCE 8x
# all DCE8.x are derived from DCE8.0
SRCS+= hw_translate_dce80.c hw_factory_dce80.c

# DCE 11x
SRCS+= hw_translate_dce110.c hw_factory_dce110.c

# DCE 12x
SRCS+= hw_translate_dce120.c hw_factory_dce120.c

# DCN 1x
# ifdef CONFIG_DRM_AMD_DC_DCN1_0
SRCS+= hw_translate_dcn10.c hw_factory_dcn10.c

# Diagnostics on FPGA
SRCS+= hw_translate_diag.c hw_factory_diag.c

# dc/i2caux
SRCS+= aux_engine.c engine_base.c i2caux.c i2c_engine.c \
	i2c_generic_hw_engine.c i2c_hw_engine.c i2c_sw_engine.c
# DCE 8x family
SRCS+= i2caux_dce80.c i2c_hw_engine_dce80.c \
	i2c_sw_engine_dce80.c

# DCE 100 family
SRCS+= i2caux_dce100.c

# DCE 110 family
SRCS+= i2caux_dce110.c i2c_sw_engine_dce110.c i2c_hw_engine_dce110.c \
	aux_engine_dce110.c

# DCE 112 family
SRCS+= i2caux_dce112.c

# DCN 1.0 family
# ifdef CONFIG_DRM_AMD_DC_DCN1_0
SRCS+= i2caux_dcn10.c

# DCE 120 family
SRCS+= i2caux_dce120.c

SRCS+= i2caux_diag.c

# dc/irq
SRCS+= irq_service.c

# DCE 8x
SRCS+= irq_service_dce80.c

# DCE 11x
SRCS+= irq_service_dce110.c

# DCE 12x
SRCS+= irq_service_dce120.c

# DCN 1x
# ifdef CONFIG_DRM_AMD_DC_DCN1_0
SRCS+= irq_service_dcn10.c

# dc/virtual
SRCS+= virtual_link_encoder.c virtual_stream_encoder.c

# dc/dcn10
SRCS+= dcn10_resource.c dcn10_ipp.c dcn10_hw_sequencer.c dcn10_hw_sequencer_debug.c \
	dcn10_dpp.c dcn10_opp.c dcn10_optc.c \
	dcn10_hubp.c dcn10_mpc.c \
	dcn10_dpp_dscl.c dcn10_dpp_cm.c dcn10_cm_common.c \
	dcn10_hubbub.c dcn10_stream_encoder.c dcn10_link_encoder.c

# dc/dml
SRCS+= display_mode_lib.c display_rq_dlg_helpers.c dml1_display_rq_dlg_calc.c \
	dml_common_defs.c

# dc/dce120
SRCS+= dce120_resource.c dce120_timing_generator.c \
	dce120_hw_sequencer.c

# dc/dce112
SRCS+= dce112_compressor.c dce112_hw_sequencer.c \
	dce112_resource.c

# dc/dce110
SRCS+= dce110_timing_generator.c \
	dce110_compressor.c dce110_hw_sequencer.c dce110_resource.c \
	dce110_opp_regamma_v.c dce110_opp_csc_v.c dce110_timing_generator_v.c \
	dce110_mem_input_v.c dce110_opp_v.c dce110_transform_v.c

# dc/dce100
SRCS+= dce100_resource.c dce100_hw_sequencer.c

# dc/dce80
SRCS+= dce80_timing_generator.c dce80_hw_sequencer.c \
	dce80_resource.c

SRCS+= dc.c dc_link.c dc_resource.c dc_hw_sequencer.c dc_sink.c \
	dc_surface.c dc_link_hwss.c dc_link_dp.c dc_link_ddc.c dc_debug.c dc_stream.c
SRCS+= dc_helper.c

# modules/freesync
SRCS+= freesync.c

# modules/color
SRCS+= color_gamma.c

# modules/info_packet
SRCS+= info_packet.c

# end display core component (CONFIG_DRM_AMD_DC)

SRCS+= \
	opt_ddb.h 	\
	opt_acpi.h	\
	opt_drm.h 	\
	acpi_if.h	\
	bus_if.h	\
	device_if.h	\
	pci_if.h

# From linux
#TODO: remove when Timing Sync feature is complete
KCFLAGS+= -DBUILD_FEATURE_TIMING_SYNC=0

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/drm
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi/drm
KCFLAGS+= -I${SYSDIR}/contrib/dev/acpica/source/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/amdgpu
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/dc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/dc/inc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/dc/inc/hw
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/modules/inc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/modules/freesync
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/modules/color
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/amdgpu_dm
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/include/asic_reg
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/inc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/hwmgr
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/smumgr
KCFLAGS+= -I${SYSDIR}/dev/drm/scheduler
KCFLAGS+= -include ${SYSDIR}/dev/drm/kconfig.h

.include <bsd.kmod.mk>

dcn_calcs.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dcn_calc_auto.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dcn_calc_math.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dcn20_resource.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dcn21_resource.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dml1_display_rq_dlg_calc.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}

dml_common_defs.o:
	${CC} ${CFLAGS} ${KCFLAGS} -mhard-float -msse -mpreferred-stack-boundary=4 -c ${.IMPSRC}
