/**
 * Copyright (c) 2013 Larisa Grigore.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/shm.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sysexits.h>
#include <libutil.h>

#include "sysvipc_hash.h"
#include "sysvipc_sockets.h"
#include "utilsd.h"
#include "shmd.h"

#define MAX_CLIENTS	256

void usage(void);


struct pollfd poll_fds[MAX_CLIENTS];
struct client *clients[MAX_CLIENTS];
int nr_poll_fds;

struct hashtable *clientshash = NULL;

int sysvd_debug;
int sysvd_daemon;

static int
remove_sysv_dir(void) 
{
	/*
	 * It is not necessary to check if the dir is empty and delete all files
	 * in it. Every time a client or the daemon exists all fds are closed
	 * and all resources are deleted (the daemon calls unlink after open a
	 * file for a sysv resource.
	 */
	return (rmdir(DIRPATH));
}

static int
create_sysv_dir(void) 
{
	remove_sysv_dir();
	return (mkdir(DIRPATH, 0600));
}

static int
daemon_init(void)
{
	int error;
	int socket_fd;

	/* Create and init structures used for clients. */
	clientshash = _hash_init(MAX_CLIENTS);
	if (!clientshash)
		return (-1);

	/* Create sysv resources directory. */
	error = create_sysv_dir();
	if (error) {
		sysvd_print_err("You must first remove %s dir\n",
				DIRPATH);
		goto err;
	}

	/* Open socket used to receive connections. */
	unlink(LISTEN_SOCKET_FILE);
	umask(0);
	int fd_tmp = open(LISTEN_SOCKET_FILE, O_EXCL | O_CREAT, 0666);
	if (fd_tmp < 0) {
		sysvd_print_err("Could not open %s\n", LISTEN_SOCKET_FILE);
		goto err;
	}
	close(fd_tmp);

	socket_fd = init_socket(LISTEN_SOCKET_FILE);
	if (socket_fd < 0) {
		sysvd_print_err("Could not init %s socket\n", LISTEN_SOCKET_FILE);
		goto err;
	}

	poll_fds[SOCKET_FD_IDX].fd = socket_fd;
	poll_fds[SOCKET_FD_IDX].events = POLLIN | POLLPRI;
	poll_fds[SOCKET_FD_IDX].revents = 0;
	nr_poll_fds++;

	shminit();

	return (0);
err:
	free(clientshash);
	return (-1);
}

static int
daemon_add_client(void) 
{
	struct client *cl;
	//int on = 1;
	struct cmsgcred cred;
	char test;

	cl = malloc(sizeof(*cl));
	if (!cl) {
		sysvd_print_err("malloc");
		return (-1);
	}

	cl->undoid = -1;

	/* Segments attached to a process. It is used
	 * when the process dies.
	 */
	LIST_INIT(&cl->ids_attached);

	/* Init communication channel between daemon and client. */
	cl->sock = handle_new_connection(poll_fds[SOCKET_FD_IDX].fd);

	poll_fds[nr_poll_fds].fd = cl->sock;
	poll_fds[nr_poll_fds].events = POLLIN;
	poll_fds[nr_poll_fds].revents = 0;

	clients[nr_poll_fds] = cl;
	nr_poll_fds++;

	if(nr_poll_fds == MAX_CLIENTS) {
		sysvd_print_err("No room for another client; connection refused\n");
		poll_fds[SOCKET_FD_IDX].events = 0;
	}

	/* Get the client pid. */
	receive_msg_with_cred(cl->sock, &test, sizeof(test), &cred);
	cl->pid = cred.cmcred_pid;

	sysvd_print("total = %d...another one will be added\n", nr_poll_fds);
	sysvd_print("pid = %d conected\n", cl->pid);

	/* Verify if the client is already connected using the hashtable. */
	if (_hash_lookup(clientshash, cl->pid)) {
		errno = EEXIST;
		sysvd_print_err("client already added");
		free(cl);
		return (-1);
	}

	/* Insert client in hashtable.  */
	_hash_insert(clientshash, cl->pid, cl);

	return (0);
}

static void
daemon_remove_client(int i) 
{

	struct client *cl = clients[i];
	sysvd_print("pid %d disconected\n", cl->pid);
	sysvd_print("total = %d\n", nr_poll_fds);

	/* Close communication channels. */
	close(cl->sock);

	/* Put last client on i position. */
	if (i != nr_poll_fds - 1) {
		poll_fds[i] = poll_fds[nr_poll_fds - 1];
		clients[i] = clients[nr_poll_fds - 1];
	}

	semexit(cl->undoid);
	shmexit(cl);

	_hash_remove(clientshash, cl->pid);
	nr_poll_fds--;
	free(cl);
	cl = NULL;

	if(nr_poll_fds == MAX_CLIENTS - 1) {
		sysvd_print_err("Now another connexion can be handled\n");
		poll_fds[SOCKET_FD_IDX].events = POLLIN | POLLPRI;
	}
}

static int
daemon_handle_msg(int i) 
{
	int msg_type;
	struct shmget_msg shmget_msg;
	struct shmctl_msg shmctl_msg;
	struct shmat_msg shmat_msg;
	int shmid;
	int error;
	struct cmsgcred cred;

	int fd_send, fd_recv;
	fd_send = fd_recv = clients[i]->sock;

	msg_type = receive_type_message(fd_recv);
	sysvd_print("type = %d from %d\n", msg_type, clients[i]->pid);

	switch(msg_type) {
		case CONNEXION_CLOSED:
			sysvd_print("connection closed\n");
			return (EOF);
		case SHMGET:
		case SEMGET:
		case MSGGET:
		case UNDOGET:
			receive_msg_with_cred(fd_recv, (char *)&shmget_msg,
					sizeof(shmget_msg), &cred);
			shmid = handle_shmget(clients[i]->pid,
					&shmget_msg, &cred);

			/* Send the shmid. */
			write(fd_send, (char *)&shmid,
					sizeof(shmid));
			sysvd_print("sent %d to client %d\n",
					shmid, clients[i]->pid);
			break;
		case SHMAT:
			receive_msg_with_cred(fd_recv, (char *)&shmat_msg,
					sizeof(shmat_msg), &cred);
			error = handle_shmat(clients[i]->pid,
					&shmat_msg, &cred);

			/* Send the error after few checks. */
			write(fd_send, (char *)&error,
					sizeof(error));
			break;
		case SHMCTL:
			receive_msg_with_cred(fd_recv, (char *)&shmctl_msg,
					sizeof(shmctl_msg), &cred);
			error = handle_shmctl(&shmctl_msg, &cred);

			/* Send the error after few checks. */
			write(fd_send, (char *)&error,
					sizeof(error));
			if (error == 0 && shmctl_msg.cmd == IPC_STAT) {

				write(fd_send, (char *)&shmctl_msg.buf,
						sizeof(struct shmid_ds));
			}
			break;
		case SHMDT:
			receive_msg_with_cred(fd_recv, (char *)&shmid,
					sizeof(shmid), NULL);
			shmid = handle_shmdt(clients[i]->pid, shmid);
			break;
		default:
			break;
	}
	sysvd_print("end\n");
	return (0);
}


static int
daemon_func(void)
{
	int i;
	//int msg;
	int ret, r;

	while(1)
	{
		ret = poll(poll_fds, nr_poll_fds, INFTIM);
		if (ret < 0) {
			sysvd_print_err("poll");
			return (-1);
		}
		for (i=0; (i < nr_poll_fds) && ret; i++) {
			if (poll_fds[i].revents == 0)
				continue;
			ret--;

			switch(i) {
			case SOCKET_FD_IDX:
				daemon_add_client();
				break;
			default:
				r = daemon_handle_msg(i);
				if (r == EOF) {
					daemon_remove_client(i);
					i--;
				}
				break;
			}
		}
		fflush(stdout);
	}

	return (0);
}

void
usage(void)
{
	fprintf(stderr, "sysvipcd [-df] [-p pidfile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int c;
	int error;
	char *pidfilename = NULL;
	struct pidfh *pfh = NULL;

	sysvd_debug = 0;
	sysvd_daemon = 1;

	while ((c = getopt(argc,argv,"dfp:")) !=-1) {
		switch(c) {
		case 'd':
			sysvd_debug = 1;
			sysvd_daemon = 0;
			break;
		case 'f':
			sysvd_daemon = 0;
			break;
		case 'p':
			pidfilename = optarg;
			break;
		default:
			usage();
			break;
		}
	}

#ifdef SYSV_SEMS
	sysvd_print("SYSV_SEMS defined (used for sysv sems); "
	    "a group of semaphores is protected)\n"
	    "by a rwlock and each semaphore is protected by a mutex\n");
#else
	sysvd_print("SYSV_SEMS not defined (used for sysv sems); "
	    "a group of semaphores is protected)\n"
	    "by a rwlock\n");
#endif

	sysvd_print("daemon starting\n");
	error = daemon_init();
	if (error)
		goto out;
		
	if (sysvd_daemon == 1) {
		pfh = pidfile_open(pidfilename, 600, NULL);
		daemon(1,0);
		pidfile_write(pfh);
	}

	daemon_func();

	/* It won't reach here. */
	sysvd_print("daemon finished\n");

	//shmfree();
	remove_sysv_dir();
out:
	return (0);
}
