/*
 * CAPS1.C
 *
 * Simple IPC test.  /tmp/cs1 -s in one window, /tmp/cs1 -c in another.
 *
 * $DragonFly: src/test/caps/caps1.c,v 1.1 2003/11/24 21:15:59 dillon Exp $
 */
#include <sys/types.h>
#include <libcaps/globaldata.h>
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/thread2.h>
#include <sys/caps.h>
#include <stdio.h>
#include <signal.h>

int
main(int ac, char **av)
{
    caps_port_t port;
    lwkt_msg_t msg;
    struct lwkt_port replyport;
    struct lwkt_msg  junkmsg;
    int i;
    int r;
    int quietOpt = 0;
    enum { UNKNOWN, CLIENT, SERVER } mode;

    signal(SIGPIPE, SIG_IGN);

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-')
	    continue;
	ptr += 2;
	switch(ptr[-1]) {
	case 's':
	    mode = SERVER;
	    break;
	case 'c':
	    mode = CLIENT;
	    break;
	case 'q':
	    quietOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
	    break;
	}
    }

    switch(mode) {
    case SERVER:
	port = caps_service("test", -1, 0666, 0);
	if (quietOpt == 0)
	    printf("caps_service port %p\n", port);
	while ((msg = lwkt_waitport(&port->lport, NULL)) != NULL) {
	    if (quietOpt == 0) {
		printf("received msg %p %08x\n", msg, msg->ms_flags);
		printf("replyport: %p %p\n", 
		    msg->ms_reply_port, msg->ms_reply_port->mp_replyport);
	    }
	    msg->u.ms_result = ~msg->u.ms_result;
	    lwkt_replymsg(msg, 23);
	}
	break;
    case CLIENT:
	lwkt_initport(&replyport, curthread);
	port = caps_client("test", -1, 0);
	if (quietOpt == 0)
	    printf("caps_client port %p msg %p\n", port, &junkmsg);
	if (port == NULL) {
	    printf("failed to connect\n");
	    exit(1);
	}
	for (i = 0; ; ++i) {
	    lwkt_initmsg(&junkmsg, &replyport, 0);
	    junkmsg.u.ms_result = i;
	    junkmsg.ms_msgsize = sizeof(junkmsg);
	    junkmsg.ms_maxsize = sizeof(junkmsg);
	    lwkt_beginmsg(&port->lport, &junkmsg);
	    if (caps_client_waitreply(port, &junkmsg) == NULL) {
		printf("client failed\n");
		break;
	    }
	    if (quietOpt == 0) {
		printf("reply: error=%d/23 res=%d/%d\n", 
		    junkmsg.ms_error, ~junkmsg.u.ms_result, i);
	    } else if (quietOpt == 1 && (i & 65535) == 0) {
		printf("reply: error=%d/23 res=%d/%d\n", 
		    junkmsg.ms_error, ~junkmsg.u.ms_result, i);
	    }
	}
	break;
    }
    printf("exit\n");
    return(0);
}

