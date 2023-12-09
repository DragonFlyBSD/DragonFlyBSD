/*-
 * Written by: David Jeffery
 * Copyright (c) 2002 Adaptec Inc.
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
 *
 * $FreeBSD: src/sys/dev/ips/ips.c,v 1.12 2004/05/30 04:01:29 scottl Exp $
 */

#include <dev/raid/ips/ips.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/thread2.h>
#include <sys/udev.h>
#include <machine/clock.h>

static d_open_t ips_open;
static d_close_t ips_close;
static d_ioctl_t ips_ioctl;

MALLOC_DEFINE(M_IPSBUF, "ipsbuf", "IPS driver buffer");

static struct dev_ops ips_ops = {
	{ "ips", 0, D_DISK },
	.d_open	= 	ips_open,
	.d_close =	ips_close,
	.d_ioctl =	ips_ioctl,
};

static const char *ips_adapter_name[] = {
	"N/A",
	"ServeRAID (copperhead)",
	"ServeRAID II (copperhead refresh)",
	"ServeRAID onboard (copperhead)",
	"ServeRAID onboard (copperhead)",
	"ServeRAID 3H (clarinet)",
	"ServeRAID 3L (clarinet lite)",
	"ServeRAID 4H (trombone)",
	"ServeRAID 4M (morpheus)",
	"ServeRAID 4L (morpheus lite)",
	"ServeRAID 4Mx (neo)",
	"ServeRAID 4Lx (neo lite)",
	"ServeRAID 5i II (sarasota)",
	"ServeRAID 5i (sarasota)",
	"ServeRAID 6M (marco)",
	"ServeRAID 6i (sebring)",
	"ServeRAID 7t",
	"ServeRAID 7k",
	"ServeRAID 7M",
};


static int
ips_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	ips_softc_t *sc = dev->si_drv1;

	sc->state |= IPS_DEV_OPEN;
	return 0;
}

static int
ips_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	ips_softc_t *sc = dev->si_drv1;

	sc->state &= ~IPS_DEV_OPEN;
	return 0;
}

static int
ips_ioctl(struct dev_ioctl_args *ap)
{
	ips_softc_t *sc;

	sc = ap->a_head.a_dev->si_drv1;
	return ips_ioctl_request(sc, ap->a_cmd, ap->a_data, ap->a_fflag);
}

static void
ips_cmd_dmaload(void *cmdptr, bus_dma_segment_t *segments, int segnum,
    int error)
{
	ips_command_t *command = cmdptr;

	PRINTF(10, "ips: in ips_cmd_dmaload\n");
	if (!error)
		command->command_phys_addr = segments[0].ds_addr;

}

/* is locking needed? what locking guarentees are there on removal? */
static int
ips_cmdqueue_free(ips_softc_t *sc)
{
	int i, error = -1;
	ips_command_t *command;

	crit_enter();
	if (sc->used_commands == 0) {
		for (i = 0; i < sc->max_cmds; i++) {
			command = &sc->commandarray[i];
			if (command->command_phys_addr == 0)
				continue;
			bus_dmamap_unload(sc->command_dmatag,
			    command->command_dmamap);
			bus_dmamem_free(sc->command_dmatag,
			    command->command_buffer,
			    command->command_dmamap);
			if (command->data_dmamap != NULL)
				bus_dmamap_destroy(command->data_dmatag,
				    command->data_dmamap);
		}
		error = 0;
		sc->state |= IPS_OFFLINE;
	}
	sc->staticcmd = NULL;
	kfree(sc->commandarray, M_IPSBUF);
	crit_exit();
	return error;
}

/*
 * Places all ips command structs on the free command queue.
 * The first slot is used exclusively for static commands
 * No locking as if someone else tries to access this during init,
 * we have bigger problems
 */
static int
ips_cmdqueue_init(ips_softc_t *sc)
{
	int i;
	ips_command_t *command;

	sc->commandarray = kmalloc(sizeof(sc->commandarray[0]) * sc->max_cmds,
	    M_IPSBUF, M_INTWAIT | M_ZERO);
	SLIST_INIT(&sc->free_cmd_list);
	for (i = 0; i < sc->max_cmds; i++) {
		command = &sc->commandarray[i];
		command->id = i;
		command->sc = sc;
		if (bus_dmamem_alloc(sc->command_dmatag,
		    &command->command_buffer, BUS_DMA_NOWAIT,
		    &command->command_dmamap))
			goto error;
		bus_dmamap_load(sc->command_dmatag, command->command_dmamap,
		    command->command_buffer, IPS_COMMAND_LEN, ips_cmd_dmaload,
		    command, BUS_DMA_NOWAIT);
		if (command->command_phys_addr == 0) {
			bus_dmamem_free(sc->command_dmatag,
			    command->command_buffer, command->command_dmamap);
			goto error;
		}

		if (i == 0)
			sc->staticcmd = command;
		else {
			command->data_dmatag = sc->sg_dmatag;
			if (bus_dmamap_create(command->data_dmatag, 0,
			    &command->data_dmamap))
				goto error;
			SLIST_INSERT_HEAD(&sc->free_cmd_list, command, next);
		}
	}
	sc->state &= ~IPS_OFFLINE;
	return 0;
error:
	ips_cmdqueue_free(sc);
	return ENOMEM;
}

/*
 * returns a free command struct if one is available.
 * It also blanks out anything that may be a wild pointer/value.
 * Also, command buffers are not freed.  They are
 * small so they are saved and kept dmamapped and loaded.
 */
int
ips_get_free_cmd(ips_softc_t *sc, ips_command_t **cmd, unsigned long flags)
{
	ips_command_t *command = NULL;
	int error = 0;

	crit_enter();
	if (sc->state & IPS_OFFLINE) {
		error = EIO;
		goto bail;
	}
	if ((flags & IPS_STATIC_FLAG) != 0) {
		if (sc->state & IPS_STATIC_BUSY) {
			error = EAGAIN;
			goto bail;
		}
		command = sc->staticcmd;
		sc->state |= IPS_STATIC_BUSY;
	} else {
		command = SLIST_FIRST(&sc->free_cmd_list);
		if (!command || (sc->state & IPS_TIMEOUT)) {
			error = EBUSY;
			goto bail;
		}
		SLIST_REMOVE_HEAD(&sc->free_cmd_list, next);
		sc->used_commands++;
	}
bail:
	crit_exit();
	if (error != 0)
		return error;

	bzero(&command->status, (char *)(command + 1) - (char *)(&command->status));
	bzero(command->command_buffer, IPS_COMMAND_LEN);
	*cmd = command;
	return 0;
}

/* adds a command back to the free command queue */
void
ips_insert_free_cmd(ips_softc_t *sc, ips_command_t *command)
{
	crit_enter();
	if (command == sc->staticcmd)
		sc->state &= ~IPS_STATIC_BUSY;
	else {
		SLIST_INSERT_HEAD(&sc->free_cmd_list, command, next);
		sc->used_commands--;
	}
	crit_exit();
}

static const char *
ips_diskdev_statename(u_int8_t state)
{
	static char statebuf[20];

	switch(state) {
	case IPS_LD_OFFLINE:
		return("OFFLINE");
		break;
	case IPS_LD_OKAY:
		return("OK");
		break;
	case IPS_LD_DEGRADED:
		return("DEGRADED");
		break;
	case IPS_LD_FREE:
		return("FREE");
		break;
	case IPS_LD_SYS:
		return("SYS");
		break;
	case IPS_LD_CRS:
		return("CRS");
		break;
	}
	ksprintf(statebuf, "UNKNOWN(0x%02x)", state);
	return (statebuf);
}

static int
ips_diskdev_init(ips_softc_t *sc)
{
	int i;

	for (i = 0; i < IPS_MAX_NUM_DRIVES; i++) {
		if (sc->drives[i].state == IPS_LD_FREE)
			continue;
		device_printf(sc->dev,
		    "Logical Drive %d: RAID%d sectors: %u, state %s\n", i,
		    sc->drives[i].raid_lvl, sc->drives[i].sector_count,
		    ips_diskdev_statename(sc->drives[i].state));
		if (sc->drives[i].state == IPS_LD_OKAY ||
		    sc->drives[i].state == IPS_LD_DEGRADED) {
			sc->diskdev[i] = device_add_child(sc->dev, NULL, -1);
			device_set_ivars(sc->diskdev[i], (void *)(uintptr_t)i);
		}
	}
	if (bus_generic_attach(sc->dev))
		device_printf(sc->dev, "Attaching bus failed\n");
	return 0;
}

static int
ips_diskdev_free(ips_softc_t *sc)
{
	int i;
	int error = 0;

	for (i = 0; i < IPS_MAX_NUM_DRIVES; i++) {
		if (sc->diskdev[i] != NULL) {
			error = device_delete_child(sc->dev, sc->diskdev[i]);
			if (error)
				return error;
		}
	}
	bus_generic_detach(sc->dev);
	return 0;
}

/*
 * ips_timeout is periodically called to make sure no commands sent
 * to the card have become stuck.  If it finds a stuck command, it
 * sets a flag so the driver won't start any more commands and then
 * is periodically called to see if all outstanding commands have
 * either finished or timed out.  Once timed out, an attempt to
 * reinitialize the card is made.  If that fails, the driver gives
 * up and declares the card dead.
 */
static void
ips_timeout(void *arg)
{
	ips_command_t *command;
	ips_softc_t *sc = arg;
	int i, state = 0;

	lockmgr(&sc->queue_lock, LK_EXCLUSIVE|LK_RETRY);
	command = &sc->commandarray[0];
	for (i = 0; i < sc->max_cmds; i++) {
		if (!command[i].timeout)
			continue;
		command[i].timeout--;
		if (command[i].timeout == 0) {
			if (!(sc->state & IPS_TIMEOUT)) {
				sc->state |= IPS_TIMEOUT;
				device_printf(sc->dev, "WARNING: command timeout. Adapter is in toaster mode, resetting to known state\n");
			}
			command[i].status.value = IPS_ERROR_STATUS;
			command[i].callback(&command[i]);
			/* hmm, this should be enough cleanup */
		} else
			state = 1;
	}
	if (!state && (sc->state & IPS_TIMEOUT)) {
		if (sc->ips_adapter_reinit(sc, 1)) {
			device_printf(sc->dev, "AIEE! adapter reset failed, "
			    "giving up and going home! Have a nice day.\n");
			sc->state |= IPS_OFFLINE;
			sc->state &= ~IPS_TIMEOUT;
			/*
			 * Grr, I hate this solution. I run waiting commands
			 * one at a time and error them out just before they
			 * would go to the card. This sucks.
			 */
		} else
			sc->state &= ~IPS_TIMEOUT;
	}
	if (sc->state != IPS_OFFLINE)
		callout_reset(&sc->timer, 10 * hz, ips_timeout, sc);
	lockmgr(&sc->queue_lock, LK_RELEASE);
}

/* check card and initialize it */
int
ips_adapter_init(ips_softc_t *sc)
{
	int i;
	cdev_t dev;

	DEVICE_PRINTF(1, sc->dev, "initializing\n");
	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* maxsize   */	IPS_COMMAND_LEN +
						    IPS_MAX_SG_LEN,
				/* numsegs   */	1,
				/* maxsegsize*/	IPS_COMMAND_LEN +
						    IPS_MAX_SG_LEN,
				/* flags     */	0,
				&sc->command_dmatag) != 0) {
		device_printf(sc->dev, "can't alloc command dma tag\n");
		goto error;
	}
	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* maxsize   */	IPS_MAX_IOBUF_SIZE,
				/* numsegs   */	IPS_MAX_SG_ELEMENTS,
				/* maxsegsize*/	IPS_MAX_IOBUF_SIZE,
				/* flags     */	0,
				&sc->sg_dmatag) != 0) {
		device_printf(sc->dev, "can't alloc SG dma tag\n");
		goto error;
	}
	/*
	 * create one command buffer until we know how many commands this card
	 * can handle
	 */
	sc->max_cmds = 1;
	ips_cmdqueue_init(sc);
	callout_init(&sc->timer);
	if (sc->ips_adapter_reinit(sc, 0))
		goto error;
	/* initialize ffdc values */
	microtime(&sc->ffdc_resettime);
	sc->ffdc_resetcount = 1;
	if ((i = ips_ffdc_reset(sc)) != 0) {
		device_printf(sc->dev,
		    "failed to send ffdc reset to device (%d)\n", i);
		goto error;
	}
	if ((i = ips_get_adapter_info(sc)) != 0) {
		device_printf(sc->dev, "failed to get adapter configuration "
		    "data from device (%d)\n", i);
		goto error;
	}
	/* no error check as failure doesn't matter */
	ips_update_nvram(sc);
	if (sc->adapter_type > 0 && sc->adapter_type <= IPS_ADAPTER_MAX_T) {
		device_printf(sc->dev, "adapter type: %s\n",
		    ips_adapter_name[sc->adapter_type]);
	}
	if ((i = ips_get_drive_info(sc)) != 0) {
		device_printf(sc->dev, "failed to get drive "
		    "configuration data from device (%d)\n", i);
		goto error;
	}
	ips_cmdqueue_free(sc);
	if (sc->adapter_info.max_concurrent_cmds)
		sc->max_cmds = min(128, sc->adapter_info.max_concurrent_cmds);
	else
		sc->max_cmds = 32;
	if (ips_cmdqueue_init(sc)) {
		device_printf(sc->dev,
		    "failed to initialize command buffers\n");
		goto error;
	}
	dev = make_dev(&ips_ops, device_get_unit(sc->dev),
		       UID_ROOT, GID_OPERATOR, S_IRUSR | S_IWUSR,
		       "ips%d", device_get_unit(sc->dev));
	dev->si_drv1 = sc;
	udev_dict_set_cstr(dev, "subsystem", "raid");
	udev_dict_set_cstr(dev, "disk-type", "raid");
	ips_diskdev_init(sc);
	callout_reset(&sc->timer, 10 * hz, ips_timeout, sc);
	return 0;
error:
	ips_adapter_free(sc);
	return ENXIO;
}

/*
 * see if we should reinitialize the card and wait for it to timeout
 * or complete initialization
 */
int
ips_morpheus_reinit(ips_softc_t *sc, int force)
{
	u_int32_t tmp;
	int i;

	tmp = ips_read_4(sc, MORPHEUS_REG_OISR);
	if (!force && (ips_read_4(sc, MORPHEUS_REG_OMR0) >= IPS_POST1_OK) &&
	    (ips_read_4(sc, MORPHEUS_REG_OMR1) != 0xdeadbeef) && !tmp) {
		ips_write_4(sc, MORPHEUS_REG_OIMR, 0);
		return 0;
	}
	ips_write_4(sc, MORPHEUS_REG_OIMR, 0xff);
	ips_read_4(sc, MORPHEUS_REG_OIMR);
	device_printf(sc->dev,
	    "resetting adapter, this may take up to 5 minutes\n");
	ips_write_4(sc, MORPHEUS_REG_IDR, 0x80000000);
	DELAY(5000000);
	pci_read_config(sc->dev, 0, 4);
	tmp = ips_read_4(sc, MORPHEUS_REG_OISR);
	for (i = 0; i < 45 && !(tmp & MORPHEUS_BIT_POST1); i++) {
		DELAY(1000000);
		DEVICE_PRINTF(2, sc->dev, "post1: %d\n", i);
		tmp = ips_read_4(sc, MORPHEUS_REG_OISR);
	}
	if (tmp & MORPHEUS_BIT_POST1)
		ips_write_4(sc, MORPHEUS_REG_OISR, MORPHEUS_BIT_POST1);

	if (i == 45 || ips_read_4(sc, MORPHEUS_REG_OMR0) < IPS_POST1_OK) {
		device_printf(sc->dev,
		    "Adapter error during initialization.\n");
		return 1;
	}
	for (i = 0; i < 240 && !(tmp & MORPHEUS_BIT_POST2); i++) {
		DELAY(1000000);
		DEVICE_PRINTF(2, sc->dev, "post2: %d\n", i);
		tmp = ips_read_4(sc, MORPHEUS_REG_OISR);
	}
	if (tmp & MORPHEUS_BIT_POST2)
		ips_write_4(sc, MORPHEUS_REG_OISR, MORPHEUS_BIT_POST2);

	if (i == 240 || !ips_read_4(sc, MORPHEUS_REG_OMR1)) {
		device_printf(sc->dev, "adapter failed config check\n");
		return 1;
	}
	ips_write_4(sc, MORPHEUS_REG_OIMR, 0);
	if (force && ips_clear_adapter(sc)) {
		device_printf(sc->dev, "adapter clear failed\n");
		return 1;
	}
	return 0;
}

/* clean up so we can unload the driver. */
int
ips_adapter_free(ips_softc_t *sc)
{
	int error = 0;

	if (sc->state & IPS_DEV_OPEN)
		return EBUSY;
	if ((error = ips_diskdev_free(sc)))
		return error;
	if (ips_cmdqueue_free(sc)) {
		device_printf(sc->dev,
		    "trying to exit when command queue is not empty!\n");
		return EBUSY;
	}
	DEVICE_PRINTF(1, sc->dev, "free\n");
	crit_enter();
	callout_stop(&sc->timer);
	crit_exit();
	if (sc->sg_dmatag)
		bus_dma_tag_destroy(sc->sg_dmatag);
	if (sc->command_dmatag)
		bus_dma_tag_destroy(sc->command_dmatag);
	dev_ops_remove_minor(&ips_ops, device_get_unit(sc->dev));
	return 0;
}

static int
ips_morpheus_check_intr(void *void_sc)
{
	ips_softc_t *sc = (ips_softc_t *)void_sc;
	u_int32_t oisr, iisr;
	ips_cmd_status_t status;
	ips_command_t *command;
	int cmdnumber;
	int found = 0;

	iisr =ips_read_4(sc, MORPHEUS_REG_IISR);
	oisr =ips_read_4(sc, MORPHEUS_REG_OISR);
	PRINTF(9, "interrupt registers in:%x out:%x\n", iisr, oisr);
	if (!(oisr & MORPHEUS_BIT_CMD_IRQ)) {
		DEVICE_PRINTF(2, sc->dev, "got a non-command irq\n");
		return(0);
	}
	while ((status.value = ips_read_4(sc, MORPHEUS_REG_OQPR))
	       != 0xffffffff) {
		cmdnumber = status.fields.command_id;
		command = &sc->commandarray[cmdnumber];
		command->status.value = status.value;
		command->timeout = 0;
		command->callback(command);
		DEVICE_PRINTF(9, sc->dev, "got command %d\n", cmdnumber);
		found = 1;
	}
	return(found);
}

void
ips_morpheus_intr(void *void_sc)
{
	ips_softc_t *sc = void_sc;

	lockmgr(&sc->queue_lock, LK_EXCLUSIVE|LK_RETRY);
	ips_morpheus_check_intr(sc);
	lockmgr(&sc->queue_lock, LK_RELEASE);
}

void
ips_issue_morpheus_cmd(ips_command_t *command)
{
	crit_enter();
	/* hmmm, is there a cleaner way to do this? */
	if (command->sc->state & IPS_OFFLINE) {
		crit_exit();
		command->status.value = IPS_ERROR_STATUS;
		command->callback(command);
		return;
	}
	command->timeout = 10;
	ips_write_4(command->sc, MORPHEUS_REG_IQPR, command->command_phys_addr);
	crit_exit();
}

void
ips_morpheus_poll(ips_command_t *command)
{
	uint32_t ts;

	ts = time_uptime + command->timeout;
	while (command->timeout != 0 &&
	    ips_morpheus_check_intr(command->sc) == 0 &&
	    (ts > time_uptime))
		DELAY(1000);
 }

static void
ips_copperhead_queue_callback(void *queueptr, bus_dma_segment_t *segments,
			      int segnum, int error)
{
	ips_copper_queue_t *queue = queueptr;

	if (error)
		return;
	queue->base_phys_addr = segments[0].ds_addr;
}

static int
ips_copperhead_queue_init(ips_softc_t *sc)
{
	bus_dma_tag_t dmatag = NULL;
	bus_dmamap_t dmamap = NULL;
	int error;

	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* maxsize   */	sizeof(ips_copper_queue_t),
				/* numsegs   */	1,
				/* maxsegsize*/	sizeof(ips_copper_queue_t),
				/* flags     */	0,
				&dmatag) != 0) {
		device_printf(sc->dev, "can't alloc dma tag for statue queue\n");
		error = ENOMEM;
		goto exit;
	}
	if (bus_dmamem_alloc(dmatag, (void *)&(sc->copper_queue),
	    BUS_DMA_NOWAIT, &dmamap)) {
		error = ENOMEM;
		goto exit;
	}
	bzero(sc->copper_queue, sizeof(ips_copper_queue_t));
	sc->copper_queue->dmatag = dmatag;
	sc->copper_queue->dmamap = dmamap;
	sc->copper_queue->nextstatus = 1;
	bus_dmamap_load(dmatag, dmamap, &(sc->copper_queue->status[0]),
	    IPS_MAX_CMD_NUM * 4, ips_copperhead_queue_callback,
	    sc->copper_queue, BUS_DMA_NOWAIT);
	if (sc->copper_queue->base_phys_addr == 0) {
		error = ENOMEM;
		goto exit;
	}
	ips_write_4(sc, COPPER_REG_SQSR, sc->copper_queue->base_phys_addr);
	ips_write_4(sc, COPPER_REG_SQER, sc->copper_queue->base_phys_addr +
	    IPS_MAX_CMD_NUM * 4);
	ips_write_4(sc, COPPER_REG_SQHR, sc->copper_queue->base_phys_addr + 4);
	ips_write_4(sc, COPPER_REG_SQTR, sc->copper_queue->base_phys_addr);
	return 0;
exit:
	bus_dmamem_free(dmatag, sc->copper_queue, dmamap);
	bus_dma_tag_destroy(dmatag);
	return error;
}

/*
 * see if we should reinitialize the card and wait for it to timeout or
 * complete initialization FIXME
 */
int
ips_copperhead_reinit(ips_softc_t *sc, int force)
{
	u_int32_t postcode = 0, configstatus = 0;
	int	  i, j;

	ips_write_1(sc, COPPER_REG_SCPR, 0x80);
	ips_write_1(sc, COPPER_REG_SCPR, 0);
	device_printf(sc->dev,
	    "reinitializing adapter, this could take several minutes.\n");
	for (j = 0; j < 2; j++) {
		postcode <<= 8;
		for (i = 0; i < 45; i++) {
			if (ips_read_1(sc, COPPER_REG_HISR) & COPPER_GHI_BIT) {
				postcode |= ips_read_1(sc, COPPER_REG_ISPR);
				ips_write_1(sc, COPPER_REG_HISR,
				    COPPER_GHI_BIT);
				break;
			} else
				DELAY(1000000);
		}
		if (i == 45)
			return 1;
	}
	for (j = 0; j < 2; j++) {
		configstatus <<= 8;
		for (i = 0; i < 240; i++) {
			if (ips_read_1(sc, COPPER_REG_HISR) & COPPER_GHI_BIT) {
				configstatus |= ips_read_1(sc, COPPER_REG_ISPR);
				ips_write_1(sc, COPPER_REG_HISR,
				    COPPER_GHI_BIT);
				break;
			} else
				DELAY(1000000);
		}
		if (i == 240)
			return 1;
	}
	for (i = 0; i < 240; i++) {
		if (!(ips_read_1(sc, COPPER_REG_CBSP) & COPPER_OP_BIT))
			break;
		else
			DELAY(1000000);
	}
	if (i == 240)
		return 1;
	ips_write_2(sc, COPPER_REG_CCCR, 0x1000 | COPPER_ILE_BIT);
	ips_write_1(sc, COPPER_REG_SCPR, COPPER_EBM_BIT);
	ips_copperhead_queue_init(sc);
	ips_write_1(sc, COPPER_REG_HISR, COPPER_GHI_BIT);
	i = ips_read_1(sc, COPPER_REG_SCPR);
	ips_write_1(sc, COPPER_REG_HISR, COPPER_EI_BIT);
	if (configstatus == 0) {
		device_printf(sc->dev, "adapter initialization failed\n");
		return 1;
	}
	if (force && ips_clear_adapter(sc)) {
		device_printf(sc->dev, "adapter clear failed\n");
		return 1;
	}
	return 0;
}

static u_int32_t
ips_copperhead_cmd_status(ips_softc_t *sc)
{
	u_int32_t value;
	int statnum;

	statnum = sc->copper_queue->nextstatus++;
	if (sc->copper_queue->nextstatus == IPS_MAX_CMD_NUM)
		sc->copper_queue->nextstatus = 0;
	crit_enter();
	value = sc->copper_queue->status[statnum];
	ips_write_4(sc, COPPER_REG_SQTR, sc->copper_queue->base_phys_addr +
	    4 * statnum);
	crit_exit();
	return value;
}

void
ips_copperhead_intr(void *void_sc)
{
	ips_softc_t *sc = (ips_softc_t *)void_sc;
	ips_cmd_status_t status;
	int cmdnumber;

	lockmgr(&sc->queue_lock, LK_EXCLUSIVE|LK_RETRY);
	while (ips_read_1(sc, COPPER_REG_HISR) & COPPER_SCE_BIT) {
		status.value = ips_copperhead_cmd_status(sc);
		cmdnumber = status.fields.command_id;
		sc->commandarray[cmdnumber].status.value = status.value;
		sc->commandarray[cmdnumber].timeout = 0;
		sc->commandarray[cmdnumber].callback(&(sc->commandarray[cmdnumber]));
		PRINTF(9, "ips: got command %d\n", cmdnumber);
	}
	lockmgr(&sc->queue_lock, LK_RELEASE);
	return;
}

void
ips_issue_copperhead_cmd(ips_command_t *command)
{
	int i;

	crit_enter();
	/* hmmm, is there a cleaner way to do this? */
	if (command->sc->state & IPS_OFFLINE) {
		crit_exit();
		command->status.value = IPS_ERROR_STATUS;
		command->callback(command);
		return;
	}
	command->timeout = 10;
	for (i = 0; ips_read_4(command->sc, COPPER_REG_CCCR) & COPPER_SEM_BIT;
	    i++) {
		if (i == 20) {
			kprintf("sem bit still set, can't send a command\n");
			crit_exit();
			return;
		}
		DELAY(500);	/* need to do a delay here */
	}
	ips_write_4(command->sc, COPPER_REG_CCSAR, command->command_phys_addr);
	ips_write_2(command->sc, COPPER_REG_CCCR, COPPER_CMD_START);
	crit_exit();
}

void
ips_copperhead_poll(ips_command_t *command)
{
	kprintf("ips: cmd polling not implemented for copperhead devices\n");
}
