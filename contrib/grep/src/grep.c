#include <config.h>
#include "search.h"

static void
Gcompile (char const *pattern, size_t size)
{
  GEAcompile (pattern, size, RE_SYNTAX_GREP | RE_NO_EMPTY_RANGES);
}

static void
Ecompile (char const *pattern, size_t size)
{
  GEAcompile (pattern, size, RE_SYNTAX_POSIX_EGREP | RE_NO_EMPTY_RANGES);
}

static void
Acompile (char const *pattern, size_t size)
{
  GEAcompile (pattern, size, RE_SYNTAX_AWK);
}

struct matcher const matchers[] = {
  { "grep",    Gcompile, EGexecute },
  { "egrep",   Ecompile, EGexecute },
  { "awk",     Acompile, EGexecute },
  { "fgrep",   Fcompile, Fexecute },
  { "perl",    Pcompile, Pexecute },
  { NULL, NULL, NULL },
};

const char before_options[] =
N_("PATTERN is, by default, a basic regular expression (BRE).\n");
const char after_options[] =
N_("'egrep' means 'grep -E'.  'fgrep' means 'grep -F'.\n\
Direct invocation as either 'egrep' or 'fgrep' is deprecated.\n");
