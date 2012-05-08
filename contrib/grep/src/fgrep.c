#include <config.h>
#include "search.h"

struct matcher const matchers[] = {
  { "fgrep", Fcompile, Fexecute },
  { NULL, NULL, NULL },
};

const char before_options[] =
N_("PATTERN is a set of newline-separated fixed strings.\n");
const char after_options[] =
N_("Invocation as 'fgrep' is deprecated; use 'grep -F' instead.\n");
