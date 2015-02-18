#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_dl.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NADDRS		128

/* XXX _SIZEOF_ADDR_IFREQ should accept ptr */
#define _SIZEOF_ADDR_IFREQ1(ifr) \
	((ifr)->ifr_addr.sa_len > sizeof(struct sockaddr) ? \
	 (sizeof(struct ifreq) - sizeof(struct sockaddr) + \
	  (ifr)->ifr_addr.sa_len) : sizeof(struct ifreq))

static void
usage(const char *cmd)
{
	fprintf(stderr, "%s [-n naddrs]\n", cmd);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct ifconf ifc;
	struct ifreq *ifr;
	caddr_t pos;
	int s, naddrs, opt;

	naddrs = NADDRS;

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			naddrs = strtol(optarg, NULL, 10);
			break;
		default:
			usage(argv[0]);
		}
	}
	if (naddrs <= 0)
		usage(argv[0]);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(2, "socket failed");

	memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_len = sizeof(struct sockaddr_storage) * naddrs;
	ifc.ifc_buf = malloc(ifc.ifc_len);
	if (ifc.ifc_buf == NULL)
		err(2, "malloc failed");

	if (ioctl(s, SIOCGIFCONF, &ifc, sizeof(ifc)) < 0)
		err(2, "ioctl failed");

	pos = ifc.ifc_buf;
	while (ifc.ifc_len >= (int)sizeof(*ifr)) {
		int len;

		ifr = (struct ifreq *)pos;
		len = _SIZEOF_ADDR_IFREQ1(ifr);
		if (ifc.ifc_len < len)
			err(2, "invalid ifc.ifc_len");

		if (ifr->ifr_addr.sa_family == AF_UNSPEC) {
			printf("%s: no address\n", ifr->ifr_name);
		} else if (ifr->ifr_addr.sa_family == AF_INET ||
		    ifr->ifr_addr.sa_family == AF_INET6) {
			char addr_str[INET6_ADDRSTRLEN];
			const void *src;
			const char *ret;

			if (ifr->ifr_addr.sa_family == AF_INET) {
				const struct sockaddr_in *in =
				    (const struct sockaddr_in *)&ifr->ifr_addr;
				src = &in->sin_addr;
			} else {
				const struct sockaddr_in6 *in6 =
				    (const struct sockaddr_in6 *)&ifr->ifr_addr;
				src = &in6->sin6_addr;
			}

			ret = inet_ntop(ifr->ifr_addr.sa_family, src,
			    addr_str, sizeof(addr_str));
			if (ret == NULL)
				err(2, "inet_ntop failed");
			printf("%s: inet%c %s\n", ifr->ifr_name,
			    ifr->ifr_addr.sa_family == AF_INET ? '4' : '6',
			    ret);
		} else if (ifr->ifr_addr.sa_family == AF_LINK) {
			const struct sockaddr_dl *dl =
			    (const struct sockaddr_dl *)&ifr->ifr_addr;

			printf("%s: link%d\n", ifr->ifr_name, dl->sdl_index);
		} else {
			printf("%s: unknown family %d\n", ifr->ifr_name,
			    ifr->ifr_addr.sa_family);
		}

		ifc.ifc_len -= len;
		pos += len;
	}
	if (ifc.ifc_len != 0)
		printf("ifc_len left %d\n", ifc.ifc_len);

	exit(0);
}
