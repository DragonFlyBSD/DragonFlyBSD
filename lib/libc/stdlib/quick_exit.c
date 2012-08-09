/*-
* Copyright (c) 2012
*	The DragonFly Project.  All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in
*    the documentation and/or other materials provided with the
*    distribution.
* 3. Neither the name of The DragonFly Project nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific, prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
* COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

#include <stdlib.h>
#include <unistd.h>

#include "libc_private.h"
#include "spinlock.h"


struct quick_exit_fn {
       struct quick_exit_fn    *next;
       void                    (*func)(void);
};

static struct quick_exit_fn *quick_exit_fns;
static spinlock_t quick_exit_spinlock;

/*
 * at_quick_exit:
 *
 *     Register a function to be called at quick_exit.
 */
int
at_quick_exit(void (*func)(void))
{
       struct quick_exit_fn *fn;

       fn = malloc(sizeof(struct quick_exit_fn));
       if (!fn)
               return (-1);

       fn->func = func;

       if (__isthreaded)
               _SPINLOCK(&quick_exit_spinlock);
       
       fn->next = quick_exit_fns;
       quick_exit_fns = fn;

       if (__isthreaded)
               _SPINUNLOCK(&quick_exit_spinlock);

       return (0);
}

/*
 * quick_exit:
 *
 *     Abandon a process. Execute all quick_exit handlers.
 */
void
quick_exit(int status)
{
       struct quick_exit_fn *fn;

       if (__isthreaded)
               _SPINLOCK(&quick_exit_spinlock);
       for (fn = quick_exit_fns; fn != NULL; fn = fn->next) {
               fn->func();
       }
       if (__isthreaded)
               _SPINUNLOCK(&quick_exit_spinlock);

       _exit(status);
}
