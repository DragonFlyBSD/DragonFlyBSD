/*-
 * Copyright (c) 1998 Michael Smith
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
 * $FreeBSD: src/sys/kern/kern_environment.c,v 1.10.2.7 2002/05/07 09:57:16 bde Exp $
 */

/*
 * The unified bootloader passes us a pointer to a preserved copy of
 * bootstrap/kernel environment variables. We convert them to a dynamic array
 * of strings later when the VM subsystem is up.
 * We make these available using sysctl for both in-kernel and
 * out-of-kernel consumers, as well as the k{get,set,unset,free,test}env()
 * functions for in-kernel consumers.
 *
 * Note that the current sysctl infrastructure doesn't allow 
 * dynamic insertion or traversal through handled spaces.  Grr.
 *
 * TODO: implement a sysctl handler to provide the functionality mentioned
 * above.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

/* exported variables */
char		*kern_envp;		/* <sys/systm.h> */

/* local variables */
char		**kenv_dynp;
int		kenv_isdynamic;
struct spinlock	kenv_dynlock;

/* constants */
MALLOC_DEFINE(M_KENV, "kenv", "kernel environment dynamic storage");
#define	KENV_DYNMAXNUM	512
#define	KENV_MAXNAMELEN	128
#define	KENV_MAXVALLEN	128

/* local prototypes */
static char	*kenv_getstring_dynamic(const char *name, int *idx);
static char	*kenv_getstring_static(const char *name);
static char	*kernenv_next(char *cp);

/*
 * Look up a string in the dynamic environment array. Must be called with
 * kenv_dynlock held.
 */
static char *
kenv_getstring_dynamic(const char *name, int *idx)
{
	char	*cp;
	int	len, i;

	len = strlen(name);
	/* note: kunsetenv() never leaves NULL holes in the array */
	for (i = 0; (cp = kenv_dynp[i]) != NULL; i++) {
		if ((strncmp(cp, name, len) == 0) && (cp[len] == '=')) {
			if (idx != NULL)
				*idx = i;
			return(cp + len + 1);
		}
	}
	return(NULL);
}

/*
 * Look up a string in the static environment array.
 */
static char *
kenv_getstring_static(const char *name)
{
	char	*cp, *ep;
	int	len;

	for (cp = kern_envp; cp != NULL; cp = kernenv_next(cp)) {
		for (ep = cp; (*ep != '=') && (*ep != 0); ep++)
			;
		if (*ep != '=')
			continue;
		len = ep - cp;
		ep++;
		if (!strncmp(name, cp, len) && name[len] == 0)
			return(ep);
	}
	return(NULL);
}

/*
 * Look up an environment variable by name.
 */
char *
kgetenv(const char *name)
{
	char	buf[KENV_MAXNAMELEN + 1 + KENV_MAXVALLEN + 1];
	char	*cp, *ret;
	int	len;

	if (kenv_isdynamic) {
		spin_lock(&kenv_dynlock);
		cp = kenv_getstring_dynamic(name, NULL);
		if (cp != NULL) {
			strcpy(buf, cp);
			spin_unlock(&kenv_dynlock);
			len = strlen(buf) + 1;
			ret = kmalloc(len, M_KENV, M_WAITOK);
			strcpy(ret, buf);
		} else {
			spin_unlock(&kenv_dynlock);
			ret = NULL;
		}
	} else
		ret = kenv_getstring_static(name);
	return(ret);
}

/*
 * Set an environment variable by name.
 */
int
ksetenv(const char *name, const char *value)
{
	char	*cp, *buf, *oldenv;
	int	namelen, vallen, i;

	if (kenv_isdynamic) {
		namelen = strlen(name) + 1;
		vallen = strlen(value) + 1;
		if ((namelen > KENV_MAXNAMELEN) || (vallen > KENV_MAXVALLEN))
			return(-1);
		buf = kmalloc(namelen + vallen, M_KENV, M_WAITOK);
		ksprintf(buf, "%s=%s", name, value);
		spin_lock(&kenv_dynlock);
		cp = kenv_getstring_dynamic(name, &i);
		if (cp != NULL) {
			/* replace existing environment variable */
			oldenv = kenv_dynp[i];
			kenv_dynp[i] = buf;
			spin_unlock(&kenv_dynlock);
			kfree(oldenv, M_KENV);
		} else {
			/* append new environment variable */
			for (i = 0; (cp = kenv_dynp[i]) != NULL; i++)
				;
			/* bounds checking */
			if (i < 0 || i >= (KENV_DYNMAXNUM - 1)) {
				kfree(buf, M_KENV);
				spin_unlock(&kenv_dynlock);
				return(-1);
			}
			kenv_dynp[i] = buf;
			kenv_dynp[i + 1] = NULL;
			spin_unlock(&kenv_dynlock);
		}
		return(0);
	} else {
		kprintf("WARNING: ksetenv: dynamic array not created yet\n");
		return(-1);
	}
}

/*
 * Unset an environment variable by name.
 */
int
kunsetenv(const char *name)
{
	char	*cp, *oldenv;
	int	i, j;

	if (kenv_isdynamic) {
		spin_lock(&kenv_dynlock);
		cp = kenv_getstring_dynamic(name, &i);
		if (cp != NULL) {
			oldenv = kenv_dynp[i];
			/* move all pointers beyond the unset one down 1 step */
			for (j = i + 1; kenv_dynp[j] != NULL; j++)
				kenv_dynp[i++] = kenv_dynp[j];
			kenv_dynp[i] = NULL;
			spin_unlock(&kenv_dynlock);
			kfree(oldenv, M_KENV);
			return(0);
		}
		spin_unlock(&kenv_dynlock);
		return(-1);
	} else {
		kprintf("WARNING: kunsetenv: dynamic array not created yet\n");
		return(-1);
	}
}

/*
 * Free an environment variable that has been copied for a consumer.
 */
void
kfreeenv(char *env)
{
	if (kenv_isdynamic)
		kfree(env, M_KENV);
}

/*
 * Test if an environment variable is defined.
 */
int
ktestenv(const char *name)
{
	char	*cp;

	if (kenv_isdynamic) {
		spin_lock(&kenv_dynlock);
		cp = kenv_getstring_dynamic(name, NULL);
		spin_unlock(&kenv_dynlock);
	} else
		cp = kenv_getstring_static(name);
	if (cp != NULL)
		return(1);
	return(0);
}

/*
 * Return a string value from an environment variable.
 */
int
kgetenv_string(const char *name, char *data, int size)
{
	char	*tmp;

	tmp = kgetenv(name);
	if (tmp != NULL) {
		strncpy(data, tmp, size);
		data[size - 1] = 0;
		kfreeenv(tmp);
		return (1);
	} else
		return (0);
}

/*
 * Return an integer value from an environment variable.
 */
int
kgetenv_int(const char *name, int *data)
{
	quad_t	tmp;
	int	rval;

	rval = kgetenv_quad(name, &tmp);
	if (rval)
		*data = (int) tmp;
	return (rval);
}

/*
 * Return a long value from an environment variable.
 */
int
kgetenv_long(const char *name, long *data)
{
	quad_t tmp;
	int rval;

	rval = kgetenv_quad(name, &tmp);
	if (rval)
		*data = (long)tmp;
	return (rval);
}

/*
 * Return an unsigned long value from an environment variable.
 */
int
kgetenv_ulong(const char *name, unsigned long *data)
{
	quad_t tmp;
	int rval;

	rval = kgetenv_quad(name, &tmp);
	if (rval)
		*data = (unsigned long) tmp;
	return (rval);
}

/*
 * Return a quad_t value from an environment variable.
 *
 * A single character kmgtKMGT extension multiplies the value
 * by 1024, 1024*1024, etc.
 */
int
kgetenv_quad(const char *name, quad_t *data)
{
	char*	value;
	char*	vtp;
	quad_t	iv;

	if ((value = kgetenv(name)) == NULL)
		return(0);

	iv = strtoq(value, &vtp, 0);
	switch(*vtp) {
	case 't':
	case 'T':
		iv <<= 10;
		/* fall through */
	case 'g':
	case 'G':
		iv <<= 10;
		/* fall through */
	case 'm':
	case 'M':
		iv <<= 10;
		/* fall through */
	case 'k':
	case 'K':
		iv <<= 10;
		++vtp;
		break;
	default:
		break;
	}

	if ((vtp == value) || (*vtp != '\0')) {
		kfreeenv(value);
		return(0);
	}

	*data = iv;
	kfreeenv(value);
	return(1);
}

/*
 * Boottime (static) kernel environment sysctl handler.
 */
static int
sysctl_kenv_boot(SYSCTL_HANDLER_ARGS)
{
	int	*name = (int *)arg1;
	u_int	namelen = arg2;
	char	*cp;
	int	i, error;

	if (kern_envp == NULL)
		return(ENOENT);
    
	name++;
	namelen--;
    
	if (namelen != 1)
		return(EINVAL);

	cp = kern_envp;
	for (i = 0; i < name[0]; i++) {
		cp = kernenv_next(cp);
		if (cp == NULL)
			break;
	}
    
	if (cp == NULL)
		return(ENOENT);
    
	error = SYSCTL_OUT(req, cp, strlen(cp) + 1);
	return (error);
}

SYSCTL_NODE(_kern, OID_AUTO, environment, CTLFLAG_RD, sysctl_kenv_boot,
	    "boottime (static) kernel  environment space");

/*
 * Find the next entry after the one which (cp) falls within, return a
 * pointer to its start or NULL if there are no more.
 */
static char *
kernenv_next(char *cp)
{
	if (cp != NULL) {
		while (*cp != 0)
			cp++;
		cp++;
		if (*cp == 0)
			cp = NULL;
	}
	return(cp);
}

/*
 * TUNABLE_INT init functions.
 */
void
tunable_int_init(void *data)
{ 
	struct tunable_int *d = (struct tunable_int *)data;

	TUNABLE_INT_FETCH(d->path, d->var);
}

void
tunable_long_init(void *data)
{
	struct tunable_long *d = (struct tunable_long *)data;

	TUNABLE_LONG_FETCH(d->path, d->var);
}

void
tunable_ulong_init(void *data)
{
	struct tunable_ulong *d = (struct tunable_ulong *)data;

	TUNABLE_ULONG_FETCH(d->path, d->var);
}

void
tunable_quad_init(void *data)
{
	struct tunable_quad *d = (struct tunable_quad *)data;

	TUNABLE_QUAD_FETCH(d->path, d->var);
}

void
tunable_str_init(void *data)
{
	struct tunable_str *d = (struct tunable_str *)data;

	TUNABLE_STR_FETCH(d->path, d->var, d->size);
}

/*
 * Create the dynamic environment array, and copy in the values from the static
 * environment as passed by the bootloader.
 */
static void
kenv_init(void *dummy)
{
	char	*cp;
	int	len, i;

	kenv_dynp = kmalloc(KENV_DYNMAXNUM * sizeof(char *), M_KENV,
			    M_WAITOK | M_ZERO);

	/* copy the static environment to our dynamic environment */
	for (i = 0, cp = kern_envp; cp != NULL; cp = kernenv_next(cp)) {
		len = strlen(cp) + 1;
		if (i < (KENV_DYNMAXNUM - 1)) {
			kenv_dynp[i] = kmalloc(len, M_KENV, M_WAITOK);
			strcpy(kenv_dynp[i++], cp);
		} else
			kprintf("WARNING: kenv: exhausted dynamic storage, "
				"ignoring string %s\n", cp);
	}
	kenv_dynp[i] = NULL;
	
	spin_init(&kenv_dynlock, "kenvdynlock");
	kenv_isdynamic = 1;
}
SYSINIT(kenv, SI_BOOT1_POST, SI_ORDER_ANY, kenv_init, NULL);
