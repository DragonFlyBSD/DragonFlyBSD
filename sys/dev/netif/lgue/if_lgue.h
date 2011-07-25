/*
 * Definitions for LG USB CDC
 */

#define LGUE_CONFIG_NO		1

#define LGUE_ALTERNATE_SETTING	1

#define LGUE_ENDPT_RX		0x0
#define LGUE_ENDPT_TX		0x1
#define LGUE_ENDPT_INTR		0x2
#define LGUE_ENDPT_MAX		0x3

#define LGUE_BUFSZ 		1600

/*
 * Internal queue entry
 */
struct lgue_queue_entry {
	/*
	int entry_len;
	char entry_tx_buf[LGUE_BUFSZ];
	*/
	struct mbuf *entry_mbuf;
	STAILQ_ENTRY(lgue_queue_entry) entry_next;
};


struct lgue_softc {
	struct arpcom lgue_arpcom;
	usbd_device_handle		lgue_udev;
	int lgue_ed[LGUE_ENDPT_MAX];
	usbd_pipe_handle lgue_ep[LGUE_ENDPT_MAX];

	int lgue_ctl_iface_no;
	usbd_interface_handle lgue_ctl_iface; /* control interface */
	int lgue_data_iface_no;
	usbd_interface_handle lgue_data_iface; /* data interface */

	char lgue_dying;
	int lgue_if_flags;

	int lgue_tx_cnt;
	usbd_xfer_handle lgue_tx_xfer;
	char *lgue_tx_buf;

	int lgue_rx_cnt;
	usbd_xfer_handle lgue_rx_xfer;
	struct timeval lgue_rx_notice;
	char *lgue_rx_buf;

	usbd_xfer_handle lgue_intr_xfer;
	char *lgue_intr_buf;

	/* Internal queue */
	STAILQ_HEAD(, lgue_queue_entry) lgue_tx_queue;
};
