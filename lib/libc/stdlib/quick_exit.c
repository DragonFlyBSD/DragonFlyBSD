
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
