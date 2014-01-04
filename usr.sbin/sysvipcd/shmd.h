#ifndef SYSVD_SHMD_H
#define SYSVD_SHMD_H

#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>

#include "sysvipc_ipc.h"
#include "sysvipc_sem.h"
#include "sysvipc_shm.h"
#include "sysvipc_msg.h"

#include "utilsd.h"
#include "perm.h"

int handle_shmget(pid_t, struct shmget_msg *, struct cmsgcred *);
int handle_shmat(pid_t, struct shmat_msg *, struct cmsgcred *);
int handle_shmdt(pid_t, int);
int handle_shmctl(struct shmctl_msg *, struct cmsgcred *);

void shminit(void);
int semexit(int);
void shmexit(struct client *);

#endif
