#ifndef __PPI_H
#define __PPI_H

#include <unistd.h>
#include <sys/queue.h>
#include <sys/ipc.h>

struct id_attached {
	int shmid;
	LIST_ENTRY(id_attached)	link;
};

struct client {
	//int fd[2];
	int sock;
	pid_t pid;
	int undoid;
	LIST_HEAD(_ids_attached, id_attached) ids_attached;
};

struct client_shm_data {
	int fd;
	pid_t pid;
//	struct ipc_perm shm_perm;
};

#ifndef _KERNEL
# include <sys/types.h>
#endif
#include <sys/ioccom.h>

#define REGISTER_DAEMON	_IO('S', 1)
#define REGISTER_TO_DAEMON	_IOR('S', 2, struct client)
#define UNREGISTER_DAEMON	_IO('S', 3)
#define INSTALL_PIPE	_IOW('S', 4, struct client)
#define CONSUME_REQUEST	_IOR('S', 5, pid_t)
#define INSTALL_FD	_IOWR('S', 5, const void *)

#endif
