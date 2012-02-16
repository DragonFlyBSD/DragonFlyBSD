/* XXX marino: disable GTHREAD_USE_WEAK to deal with gcc 4.6 error
               weakref '__gthrw_<something>' must have static linkage.
               GTHREAD is for use with GNAT, the Ada compiler, which isn't
               even switched on. */
#define GTHREAD_USE_WEAK 0

#include "gthr-posix.h"
