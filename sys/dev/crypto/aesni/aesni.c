/*-
 * Copyright (c) 2005-2008 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/crypto/aesni/aesni.c,v 1.3 2010/09/23 11:57:25 pjd Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/bus.h>
#include <sys/uio.h>
#include <dev/crypto/aesni/aesni.h>
#include "cryptodev_if.h"

struct aesni_softc {
	int32_t cid;
	uint32_t sid;
	TAILQ_HEAD(aesni_sessions_head, aesni_session) sessions;
	struct spinlock lock;
};

static int aesni_newsession(device_t, uint32_t *sidp, struct cryptoini *cri);
static int aesni_freesession(device_t, uint64_t tid);
static void aesni_freesession_locked(struct aesni_softc *sc,
    struct aesni_session *ses);

MALLOC_DEFINE(M_AESNI, "aesni_data", "AESNI Data");

static void
aesni_identify(driver_t *drv, device_t parent)
{

	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "aesni", -1) == NULL &&
	    BUS_ADD_CHILD(parent, parent, 10, "aesni", -1) == 0)
		panic("aesni: could not attach");
}

static int
aesni_probe(device_t dev)
{

	if ((cpu_feature2 & CPUID2_AESNI) == 0) {
		device_printf(dev, "No AESNI support.\n");
		return (EINVAL);
	}
	device_set_desc_copy(dev, "AES-CBC,AES-XTS");
	return (0);
}

static int
aesni_attach(device_t dev)
{
	struct aesni_softc *sc;

	sc = device_get_softc(dev);
	TAILQ_INIT(&sc->sessions);
	sc->sid = 1;
	sc->cid = crypto_get_driverid(dev, CRYPTOCAP_F_HARDWARE);
	if (sc->cid < 0) {
		device_printf(dev, "Could not get crypto driver id.\n");
		return (ENOMEM);
	}

	spin_init(&sc->lock, "aesniattach");
	crypto_register(sc->cid, CRYPTO_AES_CBC, 0, 0);
	crypto_register(sc->cid, CRYPTO_AES_XTS, 0, 0);
	return (0);
}

static int
aesni_detach(device_t dev)
{
	struct aesni_softc *sc;
	struct aesni_session *ses;

	sc = device_get_softc(dev);
	spin_lock(&sc->lock);
	TAILQ_FOREACH(ses, &sc->sessions, next) {
		if (ses->used) {
			spin_unlock(&sc->lock);
			device_printf(dev,
			    "Cannot detach, sessions still active.\n");
			return (EBUSY);
		}
	}
	while ((ses = TAILQ_FIRST(&sc->sessions)) != NULL) {
		TAILQ_REMOVE(&sc->sessions, ses, next);
		kfree(ses, M_AESNI);
	}
	spin_unlock(&sc->lock);
	spin_uninit(&sc->lock);
	crypto_unregister_all(sc->cid);
	return (0);
}

static int
aesni_newsession(device_t dev, uint32_t *sidp, struct cryptoini *cri)
{
	struct aesni_softc *sc;
	struct aesni_session *ses;
	struct cryptoini *encini;
	int error;

	if (sidp == NULL || cri == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	ses = NULL;
	encini = NULL;
	for (; cri != NULL; cri = cri->cri_next) {
		switch (cri->cri_alg) {
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_XTS:
			if (encini != NULL)
				return (EINVAL);
			encini = cri;
			break;
		default:
			return (EINVAL);
		}
	}
	if (encini == NULL)
		return (EINVAL);

	spin_lock(&sc->lock);
	/*
	 * Free sessions goes first, so if first session is used, we need to
	 * allocate one.
	 */
	ses = TAILQ_FIRST(&sc->sessions);
	if (ses == NULL || ses->used) {
		/*
		 * Release the spinlock here, since the following
		 * kmalloc(M_WAITOK) may block.  kmalloc(M_NOWAIT)
		 * is not acceptable on this code path.
		 */
		spin_unlock(&sc->lock);

		/*
		 * aesni_session must be at least AESNI_ALIGN aligned.  To
		 * make sure about that we always do a power-of-2 allocation.
		 */
		ses = kmalloc(sizeof(*ses), M_AESNI,
		    M_WAITOK | M_ZERO | M_POWEROF2);
		if ((uintptr_t)ses & (AESNI_ALIGN - 1)) {
			panic("aesni: ses %p is not %d aligned",
			    ses, AESNI_ALIGN);
		}

		spin_lock(&sc->lock);
		ses->id = sc->sid++;
	} else {
		TAILQ_REMOVE(&sc->sessions, ses, next);
	}
	ses->used = 1;
	TAILQ_INSERT_TAIL(&sc->sessions, ses, next);
	spin_unlock(&sc->lock);
	ses->algo = encini->cri_alg;

	error = aesni_cipher_setup(ses, encini);
	if (error != 0) {
		spin_lock(&sc->lock);
		aesni_freesession_locked(sc, ses);
		spin_unlock(&sc->lock);
		return (error);
	}

	*sidp = ses->id;
	return (0);
}

static void
aesni_freesession_locked(struct aesni_softc *sc, struct aesni_session *ses)
{
	uint32_t sid;

	sid = ses->id;
	TAILQ_REMOVE(&sc->sessions, ses, next);

	bzero(ses, sizeof(*ses));
	ses->id = sid;
	TAILQ_INSERT_HEAD(&sc->sessions, ses, next);
}

static int
aesni_freesession(device_t dev, uint64_t tid)
{
	struct aesni_softc *sc;
	struct aesni_session *ses;
	uint32_t sid;

	sc = device_get_softc(dev);
	sid = ((uint32_t)tid) & 0xffffffff;
	spin_lock(&sc->lock);
	TAILQ_FOREACH_REVERSE(ses, &sc->sessions, aesni_sessions_head, next) {
		if (ses->id == sid)
			break;
	}
	if (ses == NULL) {
		spin_unlock(&sc->lock);
		return (EINVAL);
	}
	aesni_freesession_locked(sc, ses);
	spin_unlock(&sc->lock);
	return (0);
}

static int
aesni_process(device_t dev, struct cryptop *crp, int hint __unused)
{
	struct aesni_softc *sc = device_get_softc(dev);
	struct aesni_session *ses = NULL;
	struct cryptodesc *crd, *enccrd;
	int error;

	error = 0;
	enccrd = NULL;

	/* Sanity check. */
	if (crp == NULL)
		return (EINVAL);

	if (crp->crp_callback == NULL || crp->crp_desc == NULL) {
		error = EINVAL;
		goto out;
	}

	for (crd = crp->crp_desc; crd != NULL; crd = crd->crd_next) {
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_XTS:
			if (enccrd != NULL) {
				error = EINVAL;
				goto out;
			}
			enccrd = crd;
			break;
		default:
			return (EINVAL);
		}
	}
	if (enccrd == NULL || (enccrd->crd_len % AES_BLOCK_LEN) != 0) {
		error = EINVAL;
		goto out;
	}

	spin_lock(&sc->lock); /* XXX: was rd lock */
	TAILQ_FOREACH_REVERSE(ses, &sc->sessions, aesni_sessions_head, next) {
		if (ses->id == (crp->crp_sid & 0xffffffff))
			break;
	}
	spin_unlock(&sc->lock); /* XXX: was rd lock */
	if (ses == NULL) {
		error = EINVAL;
		goto out;
	}

	error = aesni_cipher_process(ses, enccrd, crp);
	if (error != 0)
		goto out;

out:
	crp->crp_etype = error;
	crypto_done(crp);
	return (error);
}

uint8_t *
aesni_cipher_alloc(struct cryptodesc *enccrd, struct cryptop *crp,
    int *allocated)
{
	struct uio *uio;
	struct iovec *iov;
	uint8_t *addr;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		goto alloc;
	else if (crp->crp_flags & CRYPTO_F_IOV) {
		uio = (struct uio *)crp->crp_buf;
		if (uio->uio_iovcnt != 1)
			goto alloc;
		iov = uio->uio_iov;
		addr = (u_char *)iov->iov_base + enccrd->crd_skip;
	} else
		addr = (u_char *)crp->crp_buf;
	*allocated = 0;
	return (addr);

alloc:
	addr = kmalloc(enccrd->crd_len, M_AESNI, M_NOWAIT);
	if (addr != NULL) {
		*allocated = 1;
		crypto_copydata(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, addr);
	} else
		*allocated = 0;
	return (addr);
}

static int
aesni_cipher_setup_common(struct aesni_session *ses, const uint8_t *key,
    int keylen)
{

	switch (ses->algo) {
	case CRYPTO_AES_CBC:
		switch (keylen) {
		case 128:
			ses->rounds = AES128_ROUNDS;
			break;
		case 192:
			ses->rounds = AES192_ROUNDS;
			break;
		case 256:
			ses->rounds = AES256_ROUNDS;
			break;
		default:
			return (EINVAL);
		}
		break;
	case CRYPTO_AES_XTS:
		switch (keylen) {
		case 256:
			ses->rounds = AES128_ROUNDS;
			break;
		case 512:
			ses->rounds = AES256_ROUNDS;
			break;
		default:
			return (EINVAL);
		}
		break;
	default:
		return (EINVAL);
	}

	aesni_set_enckey(key, ses->enc_schedule, ses->rounds);
	aesni_set_deckey(ses->enc_schedule, ses->dec_schedule, ses->rounds);
	if (ses->algo == CRYPTO_AES_CBC)
		karc4random_buf(ses->iv, sizeof(ses->iv));
	else /* if (ses->algo == CRYPTO_AES_XTS) */ {
		aesni_set_enckey(key + keylen / 16, ses->xts_schedule,
		    ses->rounds);
	}

	return (0);
}

int
aesni_cipher_setup(struct aesni_session *ses, struct cryptoini *encini)
{
	int error = 0;
#if 0
	struct thread *td;
	int saved_ctx;
#endif

#if 0
	td = curthread;
	if (!is_fpu_kern_thread(0)) {
		error = fpu_kern_enter(td, &ses->fpu_ctx, FPU_KERN_NORMAL);
		saved_ctx = 1;
	} else {
		error = 0;
		saved_ctx = 0;
	}
#endif
	if (error == 0) {
		error = aesni_cipher_setup_common(ses, encini->cri_key,
		    encini->cri_klen);
#if 0
		if (saved_ctx)
			fpu_kern_leave(td, &ses->fpu_ctx);
#endif
	}
	return (error);
}

int
aesni_cipher_process(struct aesni_session *ses, struct cryptodesc *enccrd,
    struct cryptop *crp)
{
	uint8_t *buf;
	int error = 0, allocated;
#if 0
	struct thread *td;
	int saved_ctx;
#endif

	buf = aesni_cipher_alloc(enccrd, crp, &allocated);
	if (buf == NULL)
		return (ENOMEM);

#if 0
	td = curthread;
	if (!is_fpu_kern_thread(0)) {
		error = fpu_kern_enter(td, &ses->fpu_ctx, FPU_KERN_NORMAL);
		if (error != 0)
			goto out;
		saved_ctx = 1;
	} else {
		saved_ctx = 0;
		error = 0;
	}
#endif

	if ((enccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		error = aesni_cipher_setup_common(ses, enccrd->crd_key,
		    enccrd->crd_klen);
		if (error != 0)
			goto out;
	}

	if ((enccrd->crd_flags & CRD_F_ENCRYPT) != 0) {
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, ses->iv, AES_BLOCK_LEN);
		if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, AES_BLOCK_LEN, ses->iv);
		if (ses->algo == CRYPTO_AES_CBC) {
			aesni_encrypt_cbc(ses->rounds, ses->enc_schedule,
			    enccrd->crd_len, buf, buf, ses->iv);
		} else /* if (ses->algo == CRYPTO_AES_XTS) */ {
			aesni_encrypt_xts(ses->rounds, ses->enc_schedule,
			    ses->xts_schedule, enccrd->crd_len, buf, buf,
			    ses->iv);
		}
	} else {
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, ses->iv, AES_BLOCK_LEN);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, AES_BLOCK_LEN, ses->iv);
		if (ses->algo == CRYPTO_AES_CBC) {
			aesni_decrypt_cbc(ses->rounds, ses->dec_schedule,
			    enccrd->crd_len, buf, ses->iv);
		} else /* if (ses->algo == CRYPTO_AES_XTS) */ {
			aesni_decrypt_xts(ses->rounds, ses->dec_schedule,
			    ses->xts_schedule, enccrd->crd_len, buf, buf,
			    ses->iv);
		}
	}
#if 0
	if (saved_ctx)
		fpu_kern_leave(td, &ses->fpu_ctx);
#endif
	if (allocated)
		crypto_copyback(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, buf);
	if ((enccrd->crd_flags & CRD_F_ENCRYPT) != 0)
		crypto_copydata(crp->crp_flags, crp->crp_buf,
		    enccrd->crd_skip + enccrd->crd_len - AES_BLOCK_LEN,
		    AES_BLOCK_LEN, ses->iv);
 out:
	if (allocated) {
		bzero(buf, enccrd->crd_len);
		kfree(buf, M_AESNI);
	}
	return (error);
}

static device_method_t aesni_methods[] = {
	DEVMETHOD(device_identify, aesni_identify),
	DEVMETHOD(device_probe, aesni_probe),
	DEVMETHOD(device_attach, aesni_attach),
	DEVMETHOD(device_detach, aesni_detach),

	DEVMETHOD(cryptodev_newsession, aesni_newsession),
	DEVMETHOD(cryptodev_freesession, aesni_freesession),
	DEVMETHOD(cryptodev_process, aesni_process),

	DEVMETHOD_END
};

static driver_t aesni_driver = {
	"aesni",
	aesni_methods,
	sizeof(struct aesni_softc),
};
static devclass_t aesni_devclass;

DRIVER_MODULE(aesni, nexus, aesni_driver, aesni_devclass, NULL, NULL);
MODULE_VERSION(aesni, 1);
MODULE_DEPEND(aesni, crypto, 1, 1, 1);
