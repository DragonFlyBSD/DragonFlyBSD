#include <config.h>
#include "search.h"

static void
Ecompile (char const *pattern, size_t size)
{
  GEAcompile (pattern, size, RE_SYNTAX_POSIX_EGREP | RE_NO_EMPTY_RANGES);
}

struct matcher const matchers[] = {
  { "egrep", Ecompile, EGexecute },
  { NULL, NULL, NULL },
};

const char before_options[] =
N_("PATTERN is an extended regular expression (ERE).\n");
const char after_options[] =
N_("Invocation as `egrep' is deprecated; use `grep -E' instead.\n");
