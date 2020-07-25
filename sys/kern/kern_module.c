/*-
 * Copyright (c) 1997 Doug Rabson
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
 * $FreeBSD: src/sys/kern/kern_module.c,v 1.21 1999/11/08 06:53:30 peter Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/sysmsg.h>
#include <sys/sysent.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/proc.h>

MALLOC_DEFINE(M_MODULE, "module", "module data structures");

typedef TAILQ_HEAD(, module) modulelist_t;
struct module {
    TAILQ_ENTRY(module)	link;		/* chain together all modules */
    TAILQ_ENTRY(module)	flink;		/* all modules in a file */
    struct linker_file*	file;		/* file which contains this module */
    int			refs;		/* reference count */
    int			id;		/* unique id number */
    char		*name;		/* module name */
    modeventhand_t	handler;	/* event handler */
    void		*arg;		/* argument for handler */
    modspecific_t	data;		/* module specific data */
};

#define MOD_EVENT(mod, type) (mod)->handler((mod), (type), (mod)->arg)

static modulelist_t modules = TAILQ_HEAD_INITIALIZER(modules);
static struct lwkt_token mod_token = LWKT_TOKEN_INITIALIZER(mod_token);
static int nextid = 1;

static void module_shutdown(void*, int);

static int
modevent_nop(module_t mod, int what, void* arg)
{
	return 0;
}


static void
module_init(void* arg)
{
    TAILQ_INIT(&modules);
    EVENTHANDLER_REGISTER(shutdown_post_sync, module_shutdown, NULL,
			  SHUTDOWN_PRI_DEFAULT);
}

SYSINIT(module, SI_BOOT2_KLD, SI_ORDER_FIRST, module_init, 0);

static void
module_shutdown(void* arg1, int arg2)
{
    module_t mod;

    for (mod = TAILQ_FIRST(&modules); mod; mod = TAILQ_NEXT(mod, link))
	MOD_EVENT(mod, MOD_SHUTDOWN);
}

void
module_register_init(const void *arg)
{
    const moduledata_t* data = (const moduledata_t*) arg;
    int error;
    module_t mod;

    mod = module_lookupbyname(data->name);
    if (mod == NULL) {
#if 0
	panic("module_register_init: module named %s not found", data->name);
#else
	/* temporary kludge until kernel `file' attachment registers modules */
	error = module_register(data, linker_kernel_file);
	if (error)
	    panic("module_register_init: register of module failed! %d", error);
	mod = module_lookupbyname(data->name);
	if (mod == NULL)
	    panic("module_register_init: module STILL not found!");
#endif
    }
    error = MOD_EVENT(mod, MOD_LOAD);
    if (error) {
	module_unload(mod);	/* ignore error */
	module_release(mod);
	kprintf("module_register_init: MOD_LOAD (%s, %lx, %p) error %d\n",
	       data->name, (u_long)data->evhand, data->priv, error);
    }
}

int
module_register(const moduledata_t *data, linker_file_t container)
{
    size_t namelen;
    module_t newmod;

    newmod = module_lookupbyname(data->name);
    if (newmod != NULL) {
	kprintf("module_register: module %s already exists!\n", data->name);
	return EEXIST;
    }
    namelen = strlen(data->name) + 1;
    newmod = (module_t) kmalloc(sizeof(struct module) + namelen,
			       M_MODULE, M_WAITOK);

    newmod->refs = 1;
    newmod->id = nextid++;
    newmod->name = (char *) (newmod + 1);
    strcpy(newmod->name, data->name);
    newmod->handler = data->evhand ? data->evhand : modevent_nop;
    newmod->arg = data->priv;
    bzero(&newmod->data, sizeof(newmod->data));
    TAILQ_INSERT_TAIL(&modules, newmod, link);

    if (container == NULL)
	container = linker_current_file;
    if (container)
	TAILQ_INSERT_TAIL(&container->modules, newmod, flink);
    newmod->file = container;

    return 0;
}

void
module_reference(module_t mod)
{
    MOD_DPF(REFS, ("module_reference: before, refs=%d\n", mod->refs));

    mod->refs++;
}

/*
 * module_release()
 *
 *	Release ref on the module and return the new reference count.  If 0
 *	is returned, the module has been removed from its list and freed.
 */
int
module_release(module_t mod)
{
    int rc;

    if (mod->refs <= 0)
	panic("module_release: bad reference count");

    MOD_DPF(REFS, ("module_release: before, refs=%d\n", mod->refs));

    rc = --mod->refs;
    if (rc == 0) {
	TAILQ_REMOVE(&modules, mod, link);
	if (mod->file) {
	    TAILQ_REMOVE(&mod->file->modules, mod, flink);
	}
	kfree(mod, M_MODULE);
    }
    return(rc);
}

module_t
module_lookupbyname(const char* name)
{
    module_t mod;

    for (mod = TAILQ_FIRST(&modules); mod; mod = TAILQ_NEXT(mod, link)) {
	if (!strcmp(mod->name, name))
	    return mod;
    }

    return NULL;
}

module_t
module_lookupbyid(int modid)
{
    module_t mod;

    for (mod = TAILQ_FIRST(&modules); mod; mod = TAILQ_NEXT(mod, link)) {
	if (mod->id == modid)
	    return mod;
    }

    return NULL;
}

int
module_unload(module_t mod)
{
    int error;

    error = MOD_EVENT(mod, MOD_UNLOAD);
    /*sync_devs();*/
    return (error);
}

int
module_getid(module_t mod)
{
    return mod->id;
}

module_t
module_getfnext(module_t mod)
{
    return TAILQ_NEXT(mod, flink);
}

void
module_setspecific(module_t mod, modspecific_t *datap)
{
    mod->data = *datap;
}

/*
 * Syscalls.
 *
 * MPALMOSTSAFE
 */
int
sys_modnext(struct sysmsg *sysmsg, const struct modnext_args *uap)
{
    module_t mod;
    int error;

    error = 0;
    lwkt_gettoken(&mod_token);
    sysmsg->sysmsg_result = -1;
    if (uap->modid == 0) {
	mod = TAILQ_FIRST(&modules);
	if (mod)
	    sysmsg->sysmsg_result = mod->id;
	else
	    error = ENOENT;
	goto done;
    }

    mod = module_lookupbyid(uap->modid);
    if (!mod) {
	error = ENOENT;
	goto done;
    }

    if (TAILQ_NEXT(mod, link))
	sysmsg->sysmsg_result = TAILQ_NEXT(mod, link)->id;
    else
	sysmsg->sysmsg_result = 0;
done:
    lwkt_reltoken(&mod_token);

    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_modfnext(struct sysmsg *sysmsg, const struct modfnext_args *uap)
{
    module_t mod;
    int error;

    lwkt_gettoken(&mod_token);
    sysmsg->sysmsg_result = -1;

    mod = module_lookupbyid(uap->modid);
    if (!mod) {
	error = ENOENT;
	goto done;
    }

    if (TAILQ_NEXT(mod, flink))
	sysmsg->sysmsg_result = TAILQ_NEXT(mod, flink)->id;
    else
	sysmsg->sysmsg_result = 0;
    error = 0;
done:
    lwkt_reltoken(&mod_token);

    return error;
}

struct module_stat_v1 {
    int		version;	/* set to sizeof(struct module_stat) */
    char	name[MAXMODNAME];
    int		refs;
    int		id;
};

/*
 * MPALMOSTSAFE
 */
int
sys_modstat(struct sysmsg *sysmsg, const struct modstat_args *uap)
{
    module_t mod;
    int error;
    int namelen;
    int version;
    struct module_stat* stat;

    lwkt_gettoken(&mod_token);
    mod = module_lookupbyid(uap->modid);
    if (!mod) {
	error = ENOENT;
	goto out;
    }

    stat = uap->stat;

    /*
     * Check the version of the user's structure.
     */
    if ((error = copyin(&stat->version, &version, sizeof(version))) != 0)
	goto out;
    if (version != sizeof(struct module_stat_v1)
	&& version != sizeof(struct module_stat)) {
	error = EINVAL;
	goto out;
    }

    namelen = strlen(mod->name) + 1;
    if (namelen > MAXMODNAME)
	namelen = MAXMODNAME;
    if ((error = copyout(mod->name, &stat->name[0], namelen)) != 0)
	goto out;

    if ((error = copyout(&mod->refs, &stat->refs, sizeof(int))) != 0)
	goto out;
    if ((error = copyout(&mod->id, &stat->id, sizeof(int))) != 0)
	goto out;

    /*
     * >v1 stat includes module data.
     */
    if (version == sizeof(struct module_stat)) {
	if ((error = copyout(&mod->data, &stat->data, sizeof(mod->data))) != 0)
	    goto out;
    }

    sysmsg->sysmsg_result = 0;

out:
    lwkt_reltoken(&mod_token);

    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_modfind(struct sysmsg *sysmsg, const struct modfind_args *uap)
{
    int error;
    char name[MAXMODNAME];
    module_t mod;

    lwkt_gettoken(&mod_token);
    if ((error = copyinstr(uap->name, name, sizeof name, 0)) != 0)
	goto out;

    mod = module_lookupbyname(name);
    if (!mod)
	error = ENOENT;
    else
	sysmsg->sysmsg_result = mod->id;

out:
    lwkt_reltoken(&mod_token);

    return error;
}
