/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/netatm/atm_usrreq.c,v 1.6 1999/08/28 00:48:39 peter Exp $
 *	@(#) $DragonFly: src/sys/netproto/atm/atm_usrreq.c,v 1.13 2007/04/21 02:26:48 dillon Exp $
 */

/*
 * Core ATM Services
 * -----------------
 *
 * ATM DGRAM socket protocol processing
 *
 */

#include "kern_include.h"

/*
 * Local functions
 */
static void	atm_dgram_attach(netmsg_t msg);
static void	atm_dgram_control(netmsg_t msg);
static int	atm_dgram_info (caddr_t);

/*
 * New-style socket request routines
 */
struct pr_usrreqs	atm_dgram_usrreqs = {
	.pru_abort = pr_generic_notsupp,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = atm_dgram_attach,
	.pru_bind = pr_generic_notsupp,
	.pru_connect = pr_generic_notsupp,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = atm_dgram_control,
	.pru_detach = pr_generic_notsupp,
	.pru_disconnect = pr_generic_notsupp,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = pr_generic_notsupp,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = pr_generic_notsupp,
	.pru_sense = pru_sense_null,
	.pru_shutdown = pr_generic_notsupp,
	.pru_sockaddr = pr_generic_notsupp,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

/*
 * Handy common code macros
 */
#ifdef DIAGNOSTIC

#define ATM_INTRO()						\
    do {							\
	crit_enter();						\
	/*							\
	 * Stack queue should have been drained			\
	 */							\
	if (atm_stackq_head != NULL)				\
		panic("atm_usrreq: stack queue not empty");	\
    } while(0)

#else

#define ATM_INTRO()						\
    do {							\
	crit_enter();						\
    } while(0)

#endif

#define	ATM_OUTRO()						\
    out: do {							\
	/*							\
	 * Drain any deferred calls				\
	 */							\
	STACK_DRAIN();						\
	crit_exit();						\
	lwkt_replymsg(&msg->lmsg, error);			\
	return;							\
	goto out;						\
    } while(0)

/*
 * Attach protocol to socket
 *
 * Arguments:
 *	so	pointer to socket
 *	proto	protocol identifier
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	errno	error processing request - reason indicated
 *
 */
static void
atm_dgram_attach(netmsg_t msg)
{
	int error;

	ATM_INTRO();

	/*
	 * Nothing to do here for ioctl()-only sockets
	 */
	error = 0;

	ATM_OUTRO();
}


/*
 * Process ioctl system calls
 *
 * Arguments:
 *	so	pointer to socket
 *	cmd	ioctl code
 *	data	pointer to code specific parameter data area
 *	ifp	pointer to ifnet structure if it's an interface ioctl
 *	p	pointer to process
 *
 * Returns:
 *	0 	request processed
 *	errno	error processing request - reason indicated
 *
 */
static void
atm_dgram_control(netmsg_t msg)
{
	u_long cmd = msg->control.nm_cmd;
	caddr_t data = msg->control.nm_data;
	struct thread *td = msg->control.nm_td;
	int error;

	error = 0;
	ATM_INTRO();

	/*
	 * First, figure out which ioctl we're dealing with and
	 * then process it based on the sub-op code
	 */
	switch (cmd) {
	case AIOCCFG: {
		struct atmcfgreq	*acp = (struct atmcfgreq *)data;
		struct atm_pif		*pip;

		if (priv_check(td, PRIV_ROOT)) {
			error = EPERM;
			goto out;
		}

		switch (acp->acr_opcode) {

		case AIOCS_CFG_ATT:
			/*
			 * Attach signalling manager
			 */
			if ((pip = atm_pifname(acp->acr_att_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}
			error = atm_sigmgr_attach(pip, acp->acr_att_proto);
			break;

		case AIOCS_CFG_DET:
			/*
			 * Detach signalling manager
			 */
			if ((pip = atm_pifname(acp->acr_det_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}
			error = atm_sigmgr_detach(pip);
			break;

		default:
			error = EOPNOTSUPP;
			break;
		}
		break;
	}

	case AIOCADD: {
		struct atmaddreq	*aap = (struct atmaddreq *)data;
		Atm_endpoint		*epp;

		if (priv_check(td, PRIV_ROOT)) {
			error = EPERM;
			goto out;
		}

		switch (aap->aar_opcode) {

		case AIOCS_ADD_PVC:
			/*
			 * Add a PVC definition
			 */

			/*
			 * Locate requested endpoint service
			 */
			epp = aap->aar_pvc_sap > ENDPT_MAX ? NULL : 
					atm_endpoints[aap->aar_pvc_sap];
			if (epp == NULL) {
				error = ENOPROTOOPT;
				goto out;
			}

			/*
			 * Let endpoint service handle it from here
			 */
			error = (*epp->ep_ioctl)(AIOCS_ADD_PVC, data, NULL);
			break;

		case AIOCS_ADD_ARP:
			/*
			 * Add an ARP mapping
			 */
			epp = atm_endpoints[ENDPT_IP];
			if (epp == NULL) {
				error = ENOPROTOOPT;
				goto out;
			}

			/*
			 * Let IP/ATM endpoint handle this
			 */
			error = (*epp->ep_ioctl) (AIOCS_ADD_ARP, data, NULL);
			break;

		default:
			error = EOPNOTSUPP;
		}
		break;
	}

	case AIOCDEL: {
		struct atmdelreq	*adp = (struct atmdelreq *)data;
		struct atm_pif		*pip;
		struct sigmgr		*smp;
		Atm_endpoint		*epp;

		if (priv_check(td, PRIV_ROOT)) {
			error = EPERM;
			goto out;
		}

		switch (adp->adr_opcode) {

		case AIOCS_DEL_PVC:
		case AIOCS_DEL_SVC:
			/*
			 * Delete a PVC or SVC
			 */

			/*
			 * Locate appropriate sigmgr
			 */
			if ((pip = atm_pifname(adp->adr_pvc_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}
			if ((smp = pip->pif_sigmgr) == NULL) {
				error = ENOENT;
				goto out;
			}

			/*
			 * Let sigmgr handle it from here
			 */
			error = (*smp->sm_ioctl)(adp->adr_opcode, data,
					(caddr_t)pip->pif_siginst);
			break;

		case AIOCS_DEL_ARP:
			/*
			 * Delete an ARP mapping
			 */
			epp = atm_endpoints[ENDPT_IP];
			if (epp == NULL) {
				error = ENOPROTOOPT;
				goto out;
			}

			/*
			 * Let IP/ATM endpoint handle this
			 */
			error = (*epp->ep_ioctl) (AIOCS_DEL_ARP, data, NULL);
			break;

		default:
			error = EOPNOTSUPP;
		}
		break;
	}

	case AIOCSET: {
		struct atmsetreq	*asp = (struct atmsetreq *)data;
		struct atm_pif		*pip;
		struct atm_nif		*nip;
		struct sigmgr		*smp;
		struct ifnet		*ifp2;

		if (priv_check(td, PRIV_ROOT)) {
			error = EPERM;
			goto out;
		}

		switch (asp->asr_opcode) {

		case AIOCS_SET_ASV:
			/*
			 * Set an ARP server address
			 */

			/*
			 * Locate appropriate sigmgr
			 */
			if ((nip = atm_nifname(asp->asr_arp_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}
			pip = nip->nif_pif;
			if ((smp = pip->pif_sigmgr) == NULL) {
				error = ENOENT;
				goto out;
			}

			/*
			 * Let sigmgr handle it from here
			 */
			error = (*smp->sm_ioctl)(AIOCS_SET_ASV, data,
					(caddr_t)nip);
			break;

		case AIOCS_SET_MAC:
			/*
			 * Set physical interface MAC/ESI address
			 */

			/*
			 * Locate physical interface
			 */
			if ((pip = atm_pifname(asp->asr_mac_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}

			/*
			 * Interface must be detached
			 */
			if (pip->pif_sigmgr != NULL) {
				error = EADDRINUSE;
				goto out;
			}

			/*
			 * Just plunk the address into the pif
			 */
			KM_COPY((caddr_t)&asp->asr_mac_addr,
				(caddr_t)&pip->pif_macaddr,
				sizeof(struct mac_addr));
			break;

		case AIOCS_SET_NIF:
			/*
			 * Define network interfaces
			 */
			if ((pip = atm_pifname(asp->asr_nif_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}

			/*
			 * Validate interface count - logical interfaces
			 * are differentiated by the atm address selector.
			 */
			if ((asp->asr_nif_cnt <= 0) ||
			    (asp->asr_nif_cnt > 256)) {
				error = EINVAL;
				goto out;
			}

			/*
			 * Make sure prefix name is unique
			 */
			TAILQ_FOREACH(ifp2, &ifnet, if_link) {
				if (!strcmp(ifp2->if_dname, asp->asr_nif_pref)) {
					/*
					 * If this is for the interface we're
					 * (re-)defining, let it through
					 */
					for (nip = pip->pif_nif; nip;
							nip = nip->nif_pnext) {
						if (&nip->nif_if == ifp2)
							break;
					}
					if (nip)
						continue;
					error = EEXIST;
					goto out;
				}
			}

			/*
			 * Let interface handle it from here
			 */
			error = (*pip->pif_ioctl)(AIOCS_SET_NIF, data,
						  (caddr_t)pip);
			break;

		case AIOCS_SET_PRF:
			/*
			 * Set interface NSAP Prefix 
			 */

			/*
			 * Locate appropriate sigmgr
			 */
			if ((pip = atm_pifname(asp->asr_prf_intf)) == NULL) {
				error = ENXIO;
				goto out;
			}
			if ((smp = pip->pif_sigmgr) == NULL) {
				error = ENOENT;
				goto out;
			}

			/*
			 * Let sigmgr handle it from here
			 */
			error = (*smp->sm_ioctl)(AIOCS_SET_PRF, data,
					(caddr_t)pip->pif_siginst);
			break;

		default:
			error = EOPNOTSUPP;
			break;
		}
		break;
	}

	case AIOCINFO:
		error = atm_dgram_info(data);
		break;

	default:
		error = EOPNOTSUPP;
	}

	ATM_OUTRO();
}


/*
 * Process AIOCINFO ioctl system calls
 *
 * Called from a critical section.
 *
 * Arguments:
 *	data	pointer to AIOCINFO parameter structure
 *
 * Returns:
 *	0 	request processed
 *	errno	error processing request - reason indicated
 *
 */
static int
atm_dgram_info(caddr_t data)
{
	struct atminfreq	*aip = (struct atminfreq *)data;
	struct atm_pif		*pip;
	struct atm_nif		*nip;
	struct sigmgr		*smp;
	Atm_endpoint		*epp;
	int		len = aip->air_buf_len;
	int		err = 0;

	switch (aip->air_opcode) {

	case AIOCS_INF_VST:
	case AIOCS_INF_CFG:
		/*
		 * Get vendor interface information
		 */
		if (aip->air_vinfo_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((pip = atm_pifname(aip->air_vinfo_intf))) {
				err = (*pip->pif_ioctl)(aip->air_opcode, data,
						(caddr_t)pip);
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want info for every interface
			 */
			for (pip = atm_interface_head; pip; 
					pip = pip->pif_next) {
				err = (*pip->pif_ioctl)(aip->air_opcode, data,
						(caddr_t)pip);
				if (err)
					break;
			}
		}
		break;

	case AIOCS_INF_IPM:
		/*
		 * Get IP Map information
		 */
		epp = atm_endpoints[ENDPT_IP];
		if (epp) {
			err = (*epp->ep_ioctl) (AIOCS_INF_IPM, data, NULL);
		} else {
			err = ENOPROTOOPT;
		}
		break;

	case AIOCS_INF_ARP:
		/*
		 * Get ARP table information
		 */
		for (pip = atm_interface_head; pip; pip = pip->pif_next) {
			if ((smp = pip->pif_sigmgr) != NULL) {
				err = (*smp->sm_ioctl)(AIOCS_INF_ARP,
					data, (caddr_t)pip->pif_siginst);
			}
			if (err)
				break;
		}
		break;

	case AIOCS_INF_ASV:
		/*
		 * Get ARP server information
		 */
		if (aip->air_asrv_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((nip = atm_nifname(aip->air_asrv_intf))) {
				if ((smp = nip->nif_pif->pif_sigmgr) != NULL) {
					err = (*smp->sm_ioctl)(AIOCS_INF_ASV,
						data, (caddr_t)nip);
				}
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want info for all arp servers
			 */
			for (pip = atm_interface_head; pip;
					pip = pip->pif_next) {
				if ((smp = pip->pif_sigmgr) != NULL) {
					for (nip = pip->pif_nif; nip; 
							nip = nip->nif_pnext) {
						err = (*smp->sm_ioctl)
							(AIOCS_INF_ASV, data,
							(caddr_t)nip);
						if (err)
							break;
					}
					if (err)
						break;
				}
			}
		}
		break;

	case AIOCS_INF_INT:
		/*
		 * Get physical interface info
		 */
		if (aip->air_int_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((pip = atm_pifname(aip->air_int_intf))) {
				err = (*pip->pif_ioctl)(AIOCS_INF_INT,
					data, (caddr_t)pip);
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want info for every physical interface
			 */
			for (pip = atm_interface_head; pip; 
					pip = pip->pif_next) {
				err = (*pip->pif_ioctl)(AIOCS_INF_INT,
						data, (caddr_t)pip);
				if (err)
					break;
			}
		}
		break;

	case AIOCS_INF_VCC:
		/*
		 * Get VCC information
		 */
		if (aip->air_vcc_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((pip = atm_pifname(aip->air_vcc_intf))) {
				if ((smp = pip->pif_sigmgr) != NULL) {
					err = (*smp->sm_ioctl)(AIOCS_INF_VCC,
						data,
						(caddr_t)pip->pif_siginst);
				} 
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want info for every interface
			 */
			for (pip = atm_interface_head; pip; 
					pip = pip->pif_next) {
				if ((smp = pip->pif_sigmgr) != NULL) {
					err = (*smp->sm_ioctl)(AIOCS_INF_VCC,
						data,
						(caddr_t)pip->pif_siginst);
				}
				if (err)
					break;
			}
		}
		break;

	case AIOCS_INF_NIF:
		/*
		 * Get network interface info
		 */
		if (aip->air_int_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((nip = atm_nifname(aip->air_int_intf))) {
				pip = nip->nif_pif;
				err = (*pip->pif_ioctl)(AIOCS_INF_NIF,
					data, (caddr_t)nip);
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want info for every network interface
			 */
			for (pip = atm_interface_head; pip; 
					pip = pip->pif_next) {
				for (nip = pip->pif_nif; nip; 
						nip = nip->nif_pnext) {
					err = (*pip->pif_ioctl)(AIOCS_INF_NIF,
							data, (caddr_t)nip);
					if (err)
						break;
				}
				if (err)
					break;
			}
		}
		break;

	case AIOCS_INF_PIS:
		/*
		 * Get physical interface statistics
		 */
		if (aip->air_physt_intf[0] != '\0') {
			/*
			 * Interface specified
			 */
			if ((pip = atm_pifname(aip->air_physt_intf))) {
				err = (*pip->pif_ioctl)(AIOCS_INF_PIS,
					data, (caddr_t)pip);
			} else {
				err = ENXIO;
			}
		} else {
			/*
			 * Want statistics for every physical interface
			 */
			for (pip = atm_interface_head; pip; 
					pip = pip->pif_next) {
				err = (*pip->pif_ioctl)(AIOCS_INF_PIS,
						data, (caddr_t)pip);
				if (err)
					break;
			}
		}
		break;

	case AIOCS_INF_VER:
		/*
		 * Get ATM software version
		 */
		if (len < sizeof(atm_version)) {
			err = ENOSPC;
			break;
		}
		if ((err = copyout((caddr_t)&atm_version,
				aip->air_buf_addr,
				sizeof(atm_version))) != 0) {
			break;
		}
		aip->air_buf_addr += sizeof(atm_version);
		aip->air_buf_len -= sizeof(atm_version);
		break;

	default:
		err = EOPNOTSUPP;
	}

	/*
	 * Calculate returned buffer length
	 */
	aip->air_buf_len = len - aip->air_buf_len;

	return (err);
}

