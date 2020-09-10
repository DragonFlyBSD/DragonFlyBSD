/*-
 * Copyright (c) 2018 Stormshield.
 * Copyright (c) 2018 Semihalf.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/dev/tpm/tpm20.c 365144 2020-09-01 21:50:31Z mjg $
 */

#include <sys/random.h>

#include "tpm20.h"

#define TPM_HARVEST_SIZE     16
/*
 * Perform a harvest every 10 seconds.
 * Since discrete TPMs are painfully slow
 * we don't want to execute this too often
 * as the chip is likely to be used by others too.
 */
#define TPM_HARVEST_INTERVAL 10000000

MALLOC_DECLARE(M_TPM20);
MALLOC_DEFINE(M_TPM20, "tpm_buffer", "buffer for tpm 2.0 driver");

static void tpm20_discard_buffer(void *arg);
#ifdef TPM_HARVEST
static void tpm20_harvest(void *arg);
#endif
static int  tpm20_save_state(device_t dev, bool suspend);

static d_open_t		tpm20_open;
static d_close_t	tpm20_close;
static d_read_t		tpm20_read;
static d_write_t	tpm20_write;
static d_ioctl_t	tpm20_ioctl;

static struct dev_ops tpm20_ops = {
	{ "tpm20", 0, 0 },
	.d_open = tpm20_open,
	.d_close = tpm20_close,
	.d_read = tpm20_read,
	.d_write = tpm20_write,
	.d_ioctl = tpm20_ioctl,
};

int
tpm20_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tpm_sc *sc;
	size_t bytes_to_transfer;
	int result = 0;

	sc = (struct tpm_sc *)dev->si_drv1;

	callout_stop(&sc->discard_buffer_callout);
	lockmgr(&sc->dev_lock, LK_EXCLUSIVE);
	if (sc->owner_tid != uio->uio_td) {
		lockmgr(&sc->dev_lock, LK_RELEASE);
		return (EPERM);
	}

	bytes_to_transfer = MIN(sc->pending_data_length, uio->uio_resid);
	if (bytes_to_transfer > 0) {
		result = uiomove((caddr_t) sc->buf, bytes_to_transfer, uio);
		memset(sc->buf, 0, TPM_BUFSIZE);
		sc->pending_data_length = 0;
		cv_signal(&sc->buf_cv);
	} else {
		result = ETIMEDOUT;
	}

	lockmgr(&sc->dev_lock, LK_RELEASE);

	return (result);
}

int
tpm20_write(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tpm_sc *sc;
	size_t byte_count;
	int result = 0;

	sc = (struct tpm_sc *)dev->si_drv1;

	byte_count = uio->uio_resid;
	if (byte_count < TPM_HEADER_SIZE) {
		device_printf(sc->dev,
		    "Requested transfer is too small\n");
		return (EINVAL);
	}

	if (byte_count > TPM_BUFSIZE) {
		device_printf(sc->dev,
		    "Requested transfer is too large\n");
		return (E2BIG);
	}

	lockmgr(&sc->dev_lock, LK_EXCLUSIVE);

	while (sc->pending_data_length != 0)
		cv_wait(&sc->buf_cv, &sc->dev_lock);

	result = uiomove(sc->buf, byte_count, uio);
	if (result != 0) {
		lockmgr(&sc->dev_lock, LK_RELEASE);
		return (result);
	}

	result = sc->transmit(sc, byte_count);

	if (result == 0) {
		callout_reset(&sc->discard_buffer_callout,
		    TPM_READ_TIMEOUT / ustick, tpm20_discard_buffer, sc);
		sc->owner_tid = uio->uio_td;
	}

	lockmgr(&sc->dev_lock, LK_RELEASE);
	return (result);
}

static void
tpm20_discard_buffer(void *arg)
{
	struct tpm_sc *sc;

	sc = (struct tpm_sc *)arg;
	if (callout_pending(&sc->discard_buffer_callout))
		return;

	lockmgr(&sc->dev_lock, LK_EXCLUSIVE);

	memset(sc->buf, 0, TPM_BUFSIZE);
	sc->pending_data_length = 0;

	cv_signal(&sc->buf_cv);
	lockmgr(&sc->dev_lock, LK_RELEASE);

	device_printf(sc->dev,
	    "User failed to read buffer in time\n");
}

int
tpm20_open(struct dev_open_args *ap)
{

	return (0);
}

int
tpm20_close(struct dev_close_args *ap)
{

	return (0);
}

int
tpm20_ioctl(struct dev_ioctl_args *ap)
{

	return (ENOTTY);
}

int
tpm20_init(struct tpm_sc *sc)
{
	sc->buf = kmalloc(TPM_BUFSIZE, M_TPM20, M_WAITOK);
	lockinit(&sc->dev_lock, "TPM driver lock", 0, LK_CANRECURSE);
	cv_init(&sc->buf_cv, "TPM buffer cv");
	callout_init_mp(&sc->discard_buffer_callout);
#ifdef TPM_HARVEST
	sc->harvest_ticks = TPM_HARVEST_INTERVAL / ustick;
	callout_init_mp(&sc->harvest_callout);
	callout_reset(&sc->harvest_callout, 0, tpm20_harvest, sc);
#endif
	sc->pending_data_length = 0;

	sc->sc_cdev = make_dev(&tpm20_ops, device_get_unit(sc->dev),
	    UID_ROOT, GID_WHEEL, TPM_CDEV_PERM_FLAG, TPM_CDEV_NAME);
	sc->sc_cdev->si_drv1 = sc;

	return (0);

}

void
tpm20_release(struct tpm_sc *sc)
{

#ifdef TPM_HARVEST
	callout_drain(&sc->harvest_callout);
#endif

	if (sc->buf != NULL)
		kfree(sc->buf, M_TPM20);

	lockuninit(&sc->dev_lock);
	cv_destroy(&sc->buf_cv);
	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
}

int
tpm20_suspend(device_t dev)
{
	return (tpm20_save_state(dev, true));
}

int
tpm20_shutdown(device_t dev)
{
	return (tpm20_save_state(dev, false));
}

#ifdef TPM_HARVEST

/*
 * Get TPM_HARVEST_SIZE random bytes and add them
 * into system entropy pool.
 */
static void
tpm20_harvest(void *arg)
{
	struct tpm_sc *sc;
	unsigned char entropy[TPM_HARVEST_SIZE];
	uint16_t entropy_size;
	int result;
	uint8_t cmd[] = {
		0x80, 0x01,		/* TPM_ST_NO_SESSIONS tag*/
		0x00, 0x00, 0x00, 0x0c,	/* cmd length */
		0x00, 0x00, 0x01, 0x7b,	/* cmd TPM_CC_GetRandom */
		0x00, TPM_HARVEST_SIZE 	/* number of bytes requested */
	};

	sc = arg;
	lockmgr(&sc->dev_lock, LK_EXCLUSIVE);
	while (sc->pending_data_length != 0)
		cv_wait(&sc->buf_cv, &sc->dev_lock);

	memcpy(sc->buf, cmd, sizeof(cmd));
	result = sc->transmit(sc, sizeof(cmd));
	if (result != 0) {
		lockmgr(&sc->dev_lock, LK_RELEASE);
		return;
	}

	/* Ignore response size */
	sc->pending_data_length = 0;

	/* The number of random bytes we got is placed right after the header */
	entropy_size = (uint16_t) sc->buf[TPM_HEADER_SIZE + 1];
	if (entropy_size > 0) {
		entropy_size = MIN(entropy_size, TPM_HARVEST_SIZE);
		memcpy(entropy,
			sc->buf + TPM_HEADER_SIZE + sizeof(uint16_t),
			entropy_size);
	}

	lockmgr(&sc->dev_lock, LK_RELEASE);
	if (entropy_size > 0)
		add_buffer_randomness_src(entropy, entropy_size, RAND_SRC_TPM);

	callout_reset(&sc->harvest_callout, sc->harvest_ticks, tpm20_harvest, sc);
}
#endif	/* TPM_HARVEST */

static int
tpm20_save_state(device_t dev, bool suspend)
{
	struct tpm_sc *sc;
	uint8_t save_cmd[] = {
		0x80, 0x01,             /* TPM_ST_NO_SESSIONS tag*/
		0x00, 0x00, 0x00, 0x0C, /* cmd length */
		0x00, 0x00, 0x01, 0x45, /* cmd TPM_CC_Shutdown */
		0x00, 0x00              /* TPM_SU_STATE */
	};

	sc = device_get_softc(dev);

	/*
	 * Inform the TPM whether we are going to suspend or reboot/shutdown.
	 */
	if (suspend)
		save_cmd[11] = 1; /* TPM_SU_STATE */

	if (sc == NULL || sc->buf == NULL)
		return (0);

	lockmgr(&sc->dev_lock, LK_EXCLUSIVE);

	memcpy(sc->buf, save_cmd, sizeof(save_cmd));
	sc->transmit(sc, sizeof(save_cmd));

	lockmgr(&sc->dev_lock, LK_RELEASE);

	return (0);
}

int32_t
tpm20_get_timeout(uint32_t command)
{
	int32_t timeout;

	switch (command) {
		case TPM_CC_CreatePrimary:
		case TPM_CC_Create:
		case TPM_CC_CreateLoaded:
			timeout = TPM_TIMEOUT_LONG;
			break;
		case TPM_CC_SequenceComplete:
		case TPM_CC_Startup:
		case TPM_CC_SequenceUpdate:
		case TPM_CC_GetCapability:
		case TPM_CC_PCR_Extend:
		case TPM_CC_EventSequenceComplete:
		case TPM_CC_HashSequenceStart:
			timeout = TPM_TIMEOUT_C;
			break;
		default:
			timeout = TPM_TIMEOUT_B;
			break;
	}
	return timeout;
}
