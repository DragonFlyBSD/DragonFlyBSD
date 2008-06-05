/* $DragonFly: src/gnu/lib/libregex/test/xmalloc.c,v 1.2 2008/06/05 18:01:49 swildner Exp $ */

#include <stdio.h>
extern char *malloc ();

void *
xmalloc (size)
  unsigned size;
{
  char *new_mem = malloc (size);

  if (new_mem == NULL)
    {
      fprintf (stderr, "xmalloc: request for %u bytes failed.\n", size);
      abort ();
    }

  return new_mem;
}
