#if !defined SAVEDIR_H_
# define SAVEDIR_H_

#include "exclude.h"

extern char *
savedir (const char *dir, off_t name_size,
         struct exclude *, struct exclude *,
         struct exclude *);

#endif
