/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEV_DIMM_H_
#define _DEV_DIMM_H_

struct dimm_softc;
struct ksensor;

struct dimm_softc	*dimm_create(int _node, int _chan, int _slot);
int			dimm_destroy(struct dimm_softc *_sc);
void			dimm_sensor_attach(struct dimm_softc *_sc,
			    struct ksensor *_sens);
void			dimm_sensor_detach(struct dimm_softc *_sc,
			    struct ksensor *_sens);

void			dimm_set_temp_thresh(struct dimm_softc *_sc,
			    int _hiwat, int _lowat);
void			dimm_set_ecc_thresh(struct dimm_softc *_sc,
			    int _thresh);

void			dimm_sensor_temp(struct dimm_softc *_sc,
			    struct ksensor *_sens, int _temp);
void			dimm_sensor_ecc_set(struct dimm_softc *_sc,
			    struct ksensor *_sens, int _ecc_cnt,
			    boolean_t _crit);
void			dimm_sensor_ecc_add(struct dimm_softc *_sc,
			    struct ksensor *_sens, int _ecc_cnt,
			    boolean_t _crit);

#endif	/* !_DEV_DIMM_H_ */
